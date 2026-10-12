# ============================================================================
# scripts/report.gd · 冒烟测试结果汇总（人可读 + 机器可读）
# ----------------------------------------------------------------------------
# 输出：results/result.json（机器可读） + results/report.md（人可读）
# ============================================================================
extends RefCounted

# E4 推算里各信息通道的分配值（用于「哪个子项超预算」）
const ALLOC = {
	"bubbles": 0.10,
	"hudtext": 0.15,
	"signals": 0.50,
	"carry": 0.10,
	"matrix": 0.05,
	"fxaa": 0.30,
}
const ALLOC_LABEL = {
	"bubbles": "需求气泡（≤72 quad / 1 draw call / 图集 instanced）",
	"hudtext": "字幕 / 名签 / 长文本（引擎 UI）",
	"signals": "WorldSignal 世界痕迹（4 类，活跃 ≤24 处）",
	"carry": "携带物剪影（≤3 挂点 / actor）",
	"matrix": "色盲 / 高对比色彩矩阵（1 fullscreen pass）",
	"fxaa": "FXAA（默认抗锯齿）",
}


static func build(env: Dictionary, phase_stats: Array, extra: Dictionary, budget: Dictionary) -> Dictionary:
	var by_phase := {}
	for st in phase_stats:
		by_phase[String(st["phase"])] = st

	var ok_headless := String(env.get("display_driver", "")) == "headless"
	var vp_size = env.get("viewport_size", [0, 0])
	var vp_w := int(vp_size[0])
	var vp_h := int(vp_size[1])

	var base_mean := _mean_of(by_phase, "P0_base")
	var p6_mean := _mean_of(by_phase, "P6_fxaa")
	var worst_render_p95 := _q_of(by_phase, "P8_lighting", "p95_ms")
	var worst_render_max := _q_of(by_phase, "P8_lighting", "max_ms")
	var normal_p95 := _q_of(by_phase, "P6_fxaa", "p95_ms")

	var sim_budget := float(budget["sim_worst_budget_ms"])
	var worst_frame_ms := worst_render_p95 + sim_budget
	var worst_frame_max_ms := worst_render_max + sim_budget

	var sub := {}
	for k in ALLOC:
		var a := _mean_of(by_phase, _phase_for(k))
		var b := _mean_of(by_phase, _prev_phase_for(k))
		sub[k] = a - b

	var info_channel_ms := p6_mean - base_mean
	var tripwire_px := float(extra.get("tripwire_min_px", -1.0))
	var lod = extra.get("lod", {})

	var criteria := []
	criteria.append(_crit("C1", "最坏帧 p95 ≤ %.2f ms（含仿真预算 %.3f ms）" % [
			float(budget["frame_budget_ms"]), sim_budget],
		worst_frame_ms <= float(budget["frame_budget_ms"]),
		"%.3f ms" % worst_frame_ms))
	criteria.append(_crit("C2", "渲染侧信息通道增量 < %.2f ms" % float(budget["info_channel_budget_ms"]),
		info_channel_ms < float(budget["info_channel_budget_ms"]),
		"%.3f ms" % info_channel_ms))
	criteria.append(_crit("C3", "TRIPWIRE 屏幕最小宽度 ≥ %.1f px" % float(extra.get("tripwire_min_px_threshold", 1.5)),
		tripwire_px >= float(extra.get("tripwire_min_px_threshold", 1.5)),
		"%.3f px（样本 %d）" % [tripwire_px, int(extra.get("tripwire_samples", 0))]))
	criteria.append(_crit("C4", "LOD：可读性保持锥内无载体切换（N2）",
		int(lod.get("executed_in_cone", -1)) == 0,
		"executed_in_cone=%d / deferred_in_cone=%d" % [
			int(lod.get("executed_in_cone", -1)), int(lod.get("deferred_in_cone", -1))]))
	criteria.append(_crit("C5", "LOD：0–60m 内始终有可见载体（N1）",
		int(lod.get("missing_carrier", -1)) == 0,
		"missing_carrier=%d" % int(lod.get("missing_carrier", -1))))
	criteria.append(_crit("C6", "对照组：关闭 N2 闸门后应检出锥内切换（证明检测器非空转）",
		int(lod.get("executed_in_cone_control", -1)) > 0,
		"control_executed_in_cone=%d" % int(lod.get("executed_in_cone_control", -1))))

	var hard := [0, 1, 2, 3, 4]
	var verdict := "PASS"
	if ok_headless:
		verdict = "INVALID_NO_GPU"
	else:
		for i in hard:
			if not bool(criteria[i]["pass"]):
				verdict = "FAIL"
				break

	var over := []
	for k in ALLOC:
		if float(sub[k]) > float(ALLOC[k]):
			over.append(k)

	return {
		"verdict": verdict,
		"verdict_note": ("本包在 headless / 无 GPU 环境下运行，帧时间无意义" if ok_headless
			else "见 criteria 逐条判定"),
		"environment": env,
		"scene": {
			"entities": int(extra.get("entities", 0)),
			"entities_full": int(extra.get("entities_full", 0)),
			"world_signal_sites": 24,
			"terrain_m": 2000,
			"viewport": [vp_w, vp_h],
			"viewport_is_1080p": (vp_w == 1920 and vp_h == 1080),
		},
		"phases": phase_stats,
		"subitem_cost_ms": sub,
		"subitem_alloc_ms": ALLOC,
		"over_budget_subitems": over,
		"information_channel_ms": info_channel_ms,
		"frame": {
			"normal_p95_ms": normal_p95,
			"worst_render_p95_ms": worst_render_p95,
			"worst_render_max_ms": worst_render_max,
			"sim_budget_added_ms": sim_budget,
			"worst_frame_p95_ms": worst_frame_ms,
			"worst_frame_max_ms": worst_frame_max_ms,
		},
		"deviation_vs_estimate": {
			"est_worst_frame_ms": float(budget["est_worst_frame_ms"]),
			"measured_worst_frame_ms": worst_frame_ms,
			"delta_ms": worst_frame_ms - float(budget["est_worst_frame_ms"]),
			"ratio": (worst_frame_ms / float(budget["est_worst_frame_ms"])
				if float(budget["est_worst_frame_ms"]) > 0.0 else 0.0),
			"est_normal_frame_ms": float(budget["est_normal_frame_ms"]),
			"measured_normal_p95_ms": normal_p95,
			"normal_delta_ms": normal_p95 - float(budget["est_normal_frame_ms"]),
		},
		"tripwire": {
			"min_px": tripwire_px,
			"min_px_unclamped": float(extra.get("tripwire_min_px_unclamped", -1.0)),
			"samples": int(extra.get("tripwire_samples", 0)),
			"threshold_px": float(extra.get("tripwire_min_px_threshold", 1.5)),
			"world_thickness_m": 0.020,
		},
		"lod": lod,
		"fxaa_applied": bool(extra.get("fxaa_applied", true)),
		"criteria": criteria,
		"budgets": budget,
	}


static func _crit(id: String, desc: String, ok: bool, measured: String) -> Dictionary:
	return {"id": id, "desc": desc, "pass": ok, "measured": measured}


static func _phase_for(k: String) -> String:
	match k:
		"bubbles": return "P1_bubbles"
		"hudtext": return "P2_hudtext"
		"signals": return "P3_signals"
		"carry": return "P4_carry"
		"matrix": return "P5_matrix"
		"fxaa": return "P6_fxaa"
	return "P0_base"


static func _prev_phase_for(k: String) -> String:
	match k:
		"bubbles": return "P0_base"
		"hudtext": return "P1_bubbles"
		"signals": return "P2_hudtext"
		"carry": return "P3_signals"
		"matrix": return "P4_carry"
		"fxaa": return "P5_matrix"
	return "P0_base"


static func _mean_of(by_phase: Dictionary, key: String) -> float:
	if by_phase.has(key):
		return float(by_phase[key]["mean_ms"])
	return 0.0


static func _q_of(by_phase: Dictionary, key: String, field: String) -> float:
	if by_phase.has(key):
		return float(by_phase[key][field])
	return 0.0


# ---------------------------------------------------------------------------
static func to_markdown(r: Dictionary) -> String:
	var L := []
	L.append("# LowWater · GPU 渲染冒烟测试报告")
	L.append("")
	L.append("**判定：`%s`**　　%s" % [String(r["verdict"]), String(r["verdict_note"])])
	L.append("")
	L.append("生成时间：%s" % String(r["environment"].get("collected_at", "")))
	L.append("")
	L.append("## 1. 机器 / 驱动")
	L.append("")
	L.append("| 项 | 值 |")
	L.append("|---|---|")
	var e: Dictionary = r["environment"]
	L.append("| 引擎版本 | `%s` |" % String(e.get("engine_version", "")))
	L.append("| 渲染后端 | `%s` |" % String(e.get("rendering_driver", "")))
	L.append("| display_driver | `%s` |" % String(e.get("display_driver", "")))
	L.append("| GPU | %s |" % String(e.get("gpu_adapter", "")))
	L.append("| GPU API | %s |" % String(e.get("gpu_api_version", "")))
	L.append("| FXAA 可用 | %s |" % ("是" if bool(r.get("fxaa_applied", true)) else "否（后端不支持 screen_space_aa，P6 增量不含 FXAA）"))
	L.append("| 显示器 | %d×%d @ %.1f Hz |" % [
		int(e.get("screen_size", [0, 0])[0]), int(e.get("screen_size", [0, 0])[1]),
		float(e.get("refresh_hz", 0.0))])
	L.append("| 视口实际尺寸 | %d×%d%s |" % [
		int(e.get("viewport_size", [0, 0])[0]), int(e.get("viewport_size", [0, 0])[1]),
		"" if bool(r["scene"]["viewport_is_1080p"]) else "  ⚠ 非 1080p"])
	L.append("| CPU | %s（%d 线程） |" % [String(e.get("cpu_name", "")), int(e.get("cpu_threads", 0))])
	L.append("")
	L.append("## 2. 判据逐条")
	L.append("")
	L.append("| # | 判据 | 实测 | 判定 |")
	L.append("|---|---|---|---|")
	for c in r["criteria"]:
		L.append("| %s | %s | %s | %s |" % [
			String(c["id"]), String(c["desc"]), String(c["measured"]),
			"✅ PASS" if bool(c["pass"]) else "❌ FAIL"])
	L.append("")
	L.append("## 3. 帧时间（ms）")
	L.append("")
	L.append("| 阶段 | 增量内容 | mean | p50 | p95 | p99 | max |")
	L.append("|---|---|---:|---:|---:|---:|---:|")
	var inc = {
		"P0_base": "—（地形 + 120 实体 + 正午光照）",
		"P1_bubbles": "+需求气泡", "P2_hudtext": "+名签", "P3_signals": "+4 类 WorldSignal",
		"P4_carry": "+携带物剪影", "P5_matrix": "+色彩矩阵", "P6_fxaa": "+FXAA",
		"P7_camera": "+相机绕行/转头", "P8_lighting": "+昼夜四档切换",
	}
	for st in r["phases"]:
		var k := String(st["phase"])
		L.append("| %s | %s | %.3f | %.3f | %.3f | %.3f | %.3f |" % [
			k, inc.get(k, ""), float(st["mean_ms"]), float(st["p50_ms"]),
			float(st["p95_ms"]), float(st["p99_ms"]), float(st["max_ms"])])
	L.append("")
	var fr: Dictionary = r["frame"]
	L.append("- 常态帧（P6，全通道 + 静态相机）p95 = **%.3f ms**" % float(fr["normal_p95_ms"]))
	L.append("- 最坏帧渲染侧（P8）p95 = **%.3f ms** / max = %.3f ms" % [
		float(fr["worst_render_p95_ms"]), float(fr["worst_render_max_ms"])])
	L.append("- 加回仿真预算 %.3f ms → **最坏帧 = %.3f ms**（对照 16.70 ms）" % [
		float(fr["sim_budget_added_ms"]), float(fr["worst_frame_p95_ms"])])
	L.append("")
	L.append("## 4. 各子项渲染成本（差分法，ms）")
	L.append("")
	L.append("| 子项 | E4 分配 | 实测增量 | 偏差 | 判定 |")
	L.append("|---|---:|---:|---:|---|")
	var sub: Dictionary = r["subitem_cost_ms"]
	var alloc: Dictionary = r["subitem_alloc_ms"]
	for k in alloc:
		var m := float(sub.get(k, 0.0))
		var a := float(alloc[k])
		L.append("| %s | %.2f | %.3f | %+.3f | %s |" % [
			String(ALLOC_LABEL[k]), a, m, m - a, "✅" if m <= a else "❌ 超预算"])
	L.append("| **信息通道合计** | **%.2f** | **%.3f** | **%+.3f** | %s |" % [
		float(r["budgets"]["info_channel_budget_ms"]), float(r["information_channel_ms"]),
		float(r["information_channel_ms"]) - float(r["budgets"]["info_channel_budget_ms"]),
		"✅" if float(r["information_channel_ms"]) < float(r["budgets"]["info_channel_budget_ms"]) else "❌ 超预算"])
	L.append("")
	L.append("## 5. 与 E4 推算的偏差")
	L.append("")
	var dv: Dictionary = r["deviation_vs_estimate"]
	L.append("| 项 | 推算 | 实测 | 偏差 | 比值 |")
	L.append("|---|---:|---:|---:|---:|")
	L.append("| 最坏帧 | %.2f ms | %.3f ms | %+.3f ms | %.2f× |" % [
		float(dv["est_worst_frame_ms"]), float(dv["measured_worst_frame_ms"]),
		float(dv["delta_ms"]), float(dv["ratio"])])
	L.append("| 常态帧 | %.2f ms | %.3f ms | %+.3f ms | — |" % [
		float(dv["est_normal_frame_ms"]), float(dv["measured_normal_p95_ms"]),
		float(dv["normal_delta_ms"])])
	L.append("")
	L.append("## 6. TRIPWIRE ≥1.5px 钳制验证")
	L.append("")
	var tw: Dictionary = r["tripwire"]
	L.append("- 世界厚度：%.3f m；屏幕最小宽度实测：**%.3f px**（样本 %d）" % [
		float(tw["world_thickness_m"]), float(tw["min_px"]), int(tw["samples"])])
	L.append("- 未钳制（原始世界厚度 0.020 m 直接投影）：**%.3f px**" % float(tw["min_px_unclamped"]))
	L.append("- 阈值：%.1f px → %s" % [float(tw["threshold_px"]),
		"✅ 成立" if float(tw["min_px"]) >= float(tw["threshold_px"]) else "❌ 不成立，需屏幕空间最小宽度钳制"])
	L.append("")
	L.append("## 7. LOD / 可读性")
	L.append("")
	var lod: Dictionary = r["lod"]
	L.append("- N2 锥内实际切换：%d（判据 = 0）" % int(lod.get("executed_in_cone", -1)))
	L.append("- N2 因锥内被推迟的切换：%d（闸门生效的证据）" % int(lod.get("deferred_in_cone", -1)))
	L.append("- N1 0–60m 无载体：%d（判据 = 0）" % int(lod.get("missing_carrier", -1)))
	L.append("- 信息项 · 距离带指定载体缺失：%d（N2 合法推迟会造成计数，非缺陷）" % int(lod.get("band_missing", -1)))
	var ctrl_n := int(lod.get("executed_in_cone_control", -1))
	L.append("- 对照组（关闭闸门）检出锥内切换：%d" % ctrl_n)
	L.append("")
	if r["verdict"] == "FAIL":
		L.append("## 8. 超预算子项（回炉优化优先看这里）")
		L.append("")
		var over = r["over_budget_subitems"]
		if over.size() == 0:
			L.append("- 无子项单独超预算；超支来自基础场景（地形/阴影/天空，见 §3 的 P0_base）。")
		else:
			for k in over:
				L.append("- **%s**：实测 %.3f ms > 分配 %.2f ms" % [
					String(ALLOC_LABEL[k]), float(sub[k]), float(alloc[k])])
			L.append("")
			L.append("建议顺序：先砍可自由降级项（阴影分辨率/距离 → 植被与 prop 密度 → 贴图 mip / 流送距离 → 模型 LOD 与面数 → 后处理），")
			L.append("**最后**才碰 accessibility.md §7.3 的 7 项不可关闭通道。")
		L.append("")
	return "\n".join(L)
