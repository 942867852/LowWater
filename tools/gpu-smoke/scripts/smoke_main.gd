# ============================================================================
# scripts/smoke_main.gd · GPU 渲染冒烟测试主流程（纯 GDScript）
# ----------------------------------------------------------------------------
# 方法：分阶段【累积开启】各渲染子项，用「帧时间差分」测出各子项渲染成本。
#   P0 基线（地形+120 实体+光照，静态相机）
#   P1 +需求气泡   P2 +名签   P3 +4 类 WorldSignal   P4 +携带物剪影
#   P5 +色彩矩阵   P6 +FXAA                          ← P6-P0 = 信息通道总耗时
#   P7 +相机绕行/转头                                 ← LOD 切换不错乱
#   P8 +昼夜四档切换                                  ← 最坏帧
#   P9 对照组（关闭 N2 可读性锥闸门，只统计不计时）
# ----------------------------------------------------------------------------
# 判据（engine-evaluation.md §E4）：
#   ① 最坏帧 p95 ≤ 16.7ms（与推算最坏帧 10.68ms 对照，须加回仿真预算 4.880ms）
#   ② 信息通道（P6-P0）**增量** < 1.20ms
# ============================================================================
extends Node3D

const WorldBuilder = preload("res://scripts/world_builder.gd")
const Report = preload("res://scripts/report.gd")

var WARMUP_FRAMES := 90
var MEASURE_FRAMES := 300
var CONTROL_FRAMES := 180
const LIGHT_TIER_FRAMES := 60      # 每档持续帧数（60 帧 ≈ 1s @60fps）
const SIM_WORST_BUDGET_MS := 4.880 # E4：SIM_TICK 3.387 + HOURLY 1.493（本包不含）
const INFO_CHANNEL_BUDGET_MS := 1.20
const FRAME_BUDGET_MS := 16.7
const EST_WORST_FRAME_MS := 10.68
const EST_NORMAL_FRAME_MS := 5.80
const TRIPWIRE_MIN_PX := 1.5

var _b = WorldBuilder.new()
var _phase := 0
var _frame_in_phase := 0
var _last_usec := 0
var _samples := []         # 当前阶段的帧时间（us）
var _phase_stats := []     # 每阶段统计
var _tier_tick := 0
var _tripwire_min_px := 9999.0
var _tripwire_min_px_unclamped := 9999.0
var _tripwire_samples := 0
var _control_done := false
var _finished := false
var _env := {}
var _lod_p8 := {}
var _fxaa_ok := true

const PHASE_KEYS := ["P0_base", "P1_bubbles", "P2_hudtext", "P3_signals", "P4_carry",
	"P5_matrix", "P6_fxaa", "P7_camera", "P8_lighting", "P9_lod_control"]


func _ready() -> void:
	randomize()
	_parse_user_args()
	_b.build(self)
	_b.set_viewport_height(int(get_viewport().size.y))
	_apply_phase(0)
	_collect_env()
	print("[gpu-smoke] 引擎版本=", Engine.get_version_info()["string"])
	print("[gpu-smoke] display_driver=", DisplayServer.get_name())
	print("[gpu-smoke] adapter=", RenderingServer.get_video_adapter_name())
	print("[gpu-smoke] api=", RenderingServer.get_video_adapter_api_version())
	print("[gpu-smoke] 实体总数=", _b.entity_count(), " full=", _b.full_entity_count())
	print("[gpu-smoke] 视口=", get_viewport().size)


## 可选命令行参数（Godot 用户参数，写在 -- 之后）：
##   --frames=<n>   每阶段采样帧数（默认 300；想快速试跑可设 30）
##   --warmup=<n>   每阶段预热帧数（默认 90）
##   --control=<n>  对照组帧数（默认 180）
func _parse_user_args() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--frames="):
			MEASURE_FRAMES = maxi(5, int(a.substr(9)))
		elif a.begins_with("--warmup="):
			WARMUP_FRAMES = maxi(1, int(a.substr(9)))
		elif a.begins_with("--control="):
			CONTROL_FRAMES = maxi(5, int(a.substr(10)))
	print("[gpu-smoke] 采样帧数=", MEASURE_FRAMES, " 预热=", WARMUP_FRAMES, " 对照=", CONTROL_FRAMES)


func _process(_delta: float) -> void:
	if _finished:
		return
	var now := Time.get_ticks_usec()
	var dt_us := 0
	if _last_usec > 0:
		dt_us = now - _last_usec
	_last_usec = now

	var key: String = PHASE_KEYS[_phase]
	var is_control := (key == "P9_lod_control")

	if is_control:
		_frame_in_phase += 1
		_b.update(_delta, true, true)
		if _frame_in_phase >= CONTROL_FRAMES:
			_finish()
		return

	# --- 昼夜档位切换（仅 P8） ---
	if key == "P8_lighting":
		_tier_tick += 1
		if _tier_tick >= LIGHT_TIER_FRAMES:
			_tier_tick = 0
			_b.apply_day_tier(_b._tier_idx + 1)

	var camera_motion := (key == "P7_camera" or key == "P8_lighting")
	_b.update(_delta, true, camera_motion)

	# --- 绊线像素宽度采样 ---
	if key == "P7_camera" or key == "P8_lighting":
		var tw = _b.measure_tripwire_px(_b.camera)
		var px: float = float(tw["px"])
		if px > 0.0:
			_tripwire_samples += 1
			_tripwire_min_px = minf(_tripwire_min_px, px)
			var upx: float = float(tw["px_unclamped"])
			if upx > 0.0:
				_tripwire_min_px_unclamped = minf(_tripwire_min_px_unclamped, upx)

	# --- 采样 ---
	_frame_in_phase += 1
	if _frame_in_phase > WARMUP_FRAMES:
		if dt_us > 0:
			_samples.append(dt_us)
		if _samples.size() >= MEASURE_FRAMES:
			_commit_phase()
			_phase += 1
			_frame_in_phase = 0
			_samples.clear()
			if _phase >= PHASE_KEYS.size():
				_finish()
			else:
				_apply_phase(_phase)
				_last_usec = Time.get_ticks_usec()   # 丢弃切换帧


func _apply_phase(idx: int) -> void:
	var key: String = PHASE_KEYS[idx]
	var vp := get_viewport()
	# 全部关掉后按阶段累积打开
	_b.set_bubbles(false)
	_b.set_hudtext(false)
	_b.set_signals(false)
	_b.set_carry(false)
	_b.set_color_matrix(false)
	_b.set_fxaa(vp, false)
	_fxaa_ok = true
	_b.set_lod_gate(true)
	_b.apply_day_tier(1)   # 正午

	match key:
		"P0_base":
			pass
		"P1_bubbles":
			_b.set_bubbles(true)
		"P2_hudtext":
			_b.set_bubbles(true); _b.set_hudtext(true)
		"P3_signals":
			_b.set_bubbles(true); _b.set_hudtext(true); _b.set_signals(true)
		"P4_carry":
			_b.set_bubbles(true); _b.set_hudtext(true); _b.set_signals(true)
			_b.set_carry(true)
		"P5_matrix":
			_b.set_bubbles(true); _b.set_hudtext(true); _b.set_signals(true)
			_b.set_carry(true); _b.set_color_matrix(true)
		"P6_fxaa":
			_b.set_bubbles(true); _b.set_hudtext(true); _b.set_signals(true)
			_b.set_carry(true); _b.set_color_matrix(true)
			_fxaa_ok = _b.set_fxaa(vp, true)
		"P7_camera":
			_b.set_bubbles(true); _b.set_hudtext(true); _b.set_signals(true)
			_b.set_carry(true); _b.set_color_matrix(true)
			_fxaa_ok = _b.set_fxaa(vp, true)
			_b.reset_lod_counters()
		"P8_lighting":
			_b.set_bubbles(true); _b.set_hudtext(true); _b.set_signals(true)
			_b.set_carry(true); _b.set_color_matrix(true)
			_fxaa_ok = _b.set_fxaa(vp, true)
			_tier_tick = 0
		"P9_lod_control":
			# 对照组：故意关闭 N2 闸门，验证「在锥内 pop」的检测器不是空转
			_b.set_bubbles(true); _b.set_hudtext(true); _b.set_signals(true)
			_b.set_carry(true); _b.set_color_matrix(true)
			_b.set_fxaa(vp, true)
			_b.set_lod_gate(false)
			_b.reset_lod_counters()
	print("[gpu-smoke] >>> 阶段 ", key)


func _commit_phase() -> void:
	var st := _stats(_samples)
	var key: String = PHASE_KEYS[_phase]
	st["phase"] = key
	_phase_stats.append(st)
	if key == "P8_lighting":
		_lod_p8 = _b.lod_violations()
	print("[gpu-smoke]   %-16s mean=%.3f p50=%.3f p95=%.3f p99=%.3f max=%.3f (ms)" % [
		st["phase"], st["mean_ms"], st["p50_ms"], st["p95_ms"], st["p99_ms"], st["max_ms"]])


func _stats(arr: Array) -> Dictionary:
	if arr.size() == 0:
		return {"n": 0, "mean_ms": 0.0, "p50_ms": 0.0, "p95_ms": 0.0,
			"p99_ms": 0.0, "max_ms": 0.0, "min_ms": 0.0}
	var s := (arr as Array).duplicate()
	s.sort()
	var n := s.size()
	var total := 0.0
	for v in s:
		total += float(v) / 1000.0
	return {
		"n": n,
		"mean_ms": total / float(n),
		"p50_ms": _q(s, 0.50),
		"p95_ms": _q(s, 0.95),
		"p99_ms": _q(s, 0.99),
		"max_ms": float(s[n - 1]) / 1000.0,
		"min_ms": float(s[0]) / 1000.0,
	}


func _q(sorted_arr: Array, q: float) -> float:
	var n := sorted_arr.size()
	var idx := int(ceil(q * float(n))) - 1
	if idx < 0:
		idx = 0
	if idx >= n:
		idx = n - 1
	return float(sorted_arr[idx]) / 1000.0


# ---------------------------------------------------------------------------
func _collect_env() -> void:
	var vp := get_viewport()
	var driver := "unknown"
	if RenderingServer.has_method("get_current_rendering_driver"):
		driver = String(RenderingServer.call("get_current_rendering_driver"))
	var refresh := 0.0
	if DisplayServer.has_method("screen_get_refresh_rate"):
		refresh = float(DisplayServer.screen_get_refresh_rate())
	# RenderingDevice 为 null => 走的是 Compatibility（OpenGL）后端，
	# 该后端不支持 screen_space_aa（FXAA 静默失效），必须显式记账。
	var rd = null
	if RenderingServer.has_method("get_rendering_device"):
		rd = RenderingServer.get_rendering_device()
	var rd_available := (rd != null)
	if driver == "unknown":
		driver = ("forward_plus / mobile（RenderingDevice：Vulkan 或 D3D12）" if rd_available
			else "gl_compatibility（OpenGL 3，不支持 screen_space_aa）")
	_env = {
		"engine_version": String(Engine.get_version_info()["string"]),
		"display_driver": DisplayServer.get_name(),
		"rendering_driver": driver,
		"gpu_adapter": RenderingServer.get_video_adapter_name(),
		"gpu_api_version": RenderingServer.get_video_adapter_api_version(),
		"screen_size": [DisplayServer.screen_get_size().x, DisplayServer.screen_get_size().y],
		"refresh_hz": refresh,
		"viewport_size": [vp.size.x, vp.size.y],
		"os": OS.get_name(),
		"cpu_name": OS.get_processor_name(),
		"cpu_threads": OS.get_processor_count(),
		"rd_available": rd_available,
		"collected_at": Time.get_datetime_string_from_system(true),
	}


func _finish() -> void:
	if _finished:
		return
	_finished = true
	var control = _b.lod_violations()
	if _tripwire_samples == 0:
		_tripwire_min_px = -1.0   # 从未采样到（无 GPU / 视口退化）→ 判据不可判
		_tripwire_min_px_unclamped = -1.0
	var lod := {
		"deferred_in_cone": int(_lod_p8.get("deferred_in_cone", 0)),
		"executed_in_cone": int(_lod_p8.get("executed_in_cone", -1)),
		"missing_carrier": int(_lod_p8.get("missing_carrier", -1)),
		"band_missing": int(_lod_p8.get("band_missing", -1)),
		"executed_in_cone_control": int(control.get("executed_in_cone", -1)),
	}
	var r := Report.build(_env, _phase_stats, {
		"tripwire_min_px": _tripwire_min_px,
		"tripwire_min_px_unclamped": _tripwire_min_px_unclamped,
		"tripwire_samples": _tripwire_samples,
		"tripwire_min_px_threshold": TRIPWIRE_MIN_PX,
		"lod": lod,
		"fxaa_applied": (_fxaa_ok and bool(_env.get("rd_available", true))),
		"entities": _b.entity_count(),
		"entities_full": _b.full_entity_count(),
	}, {
		"frame_budget_ms": FRAME_BUDGET_MS,
		"info_channel_budget_ms": INFO_CHANNEL_BUDGET_MS,
		"sim_worst_budget_ms": SIM_WORST_BUDGET_MS,
		"est_worst_frame_ms": EST_WORST_FRAME_MS,
		"est_normal_frame_ms": EST_NORMAL_FRAME_MS,
	})
	var dir := "res://results"
	DirAccess.make_dir_recursive_absolute(dir)
	var jp := dir + "/result.json"
	var mp := dir + "/report.md"
	var f := FileAccess.open(jp, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(r, "  "))
		f.close()
	var g := FileAccess.open(mp, FileAccess.WRITE)
	if g:
		g.store_string(Report.to_markdown(r))
		g.close()
	print(Report.to_markdown(r))
	print("[gpu-smoke] 已写出：", ProjectSettings.globalize_path(jp))
	print("[gpu-smoke] 已写出：", ProjectSettings.globalize_path(mp))
	get_tree().quit()
