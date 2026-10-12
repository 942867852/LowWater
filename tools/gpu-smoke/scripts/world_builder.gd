# ============================================================================
# scripts/world_builder.gd · 冒烟测试场景构造器（纯 GDScript，零美术资产）
# ----------------------------------------------------------------------------
# 构造内容（对应 E4 推算 & accessibility.md 硬约束）：
#   · 2×2km 地形 + 300 个静态垂直元素（近似真实场景的 DC/顶点压力）
#   · 120 个实体：32 个 full 行为（会走） + 88 个简模
#   · 需求气泡（三重编码：形状 glyph / 位置钟位 / 运动）
#   · 名签（HUD 文本层）
#   · 4 类 WorldSignal 世界痕迹（REROUTE / ADD_WATCH / DEEPEN_STASH / SET_TRAP）
#   · 携带物剪影
#   · 色盲 / 高对比色彩矩阵（1 个 fullscreen pass）
#   · 昼夜光照四档（晨 / 午 / 昏 / 夜）
# ----------------------------------------------------------------------------
# 诚实声明：纯 GDScript 无法做到 accessibility.md §7.1 承诺的
# 「≤72 quad / 1 draw call / 图集 instanced」——每个气泡是一个 Sprite3D
# （一次 draw call）。因此【气泡成本会高于真实实现】，属于保守方向。
# ============================================================================
extends RefCounted

const GlyphAtlas = preload("res://scripts/glyph_atlas.gd")

const ENTITY_TOTAL := 120
const ENTITY_FULL := 32            # LOD_FULL_MAX = 32（simcore 已断言）
const PROP_COUNT := 300
const SIGNAL_SITES_PER_KIND := 6   # 4 类 × 6 处 = 24 处（活跃 ≤24 处）
const TERRAIN_SIZE := 2000.0
const TRIPWIRE_THICKNESS := 0.020
const TRIPWIRE_MIN_PX := 1.5  # m，用于验证 ≥1.5px 最小宽度钳制
const CONE_YAW_DEG := 25.0         # 可读性保持锥（accessibility §2.4 N2）
const CONE_PITCH_DEG := 20.0

# 昼夜四档（S9 §6.1 C-1 时间锚）
const DAY_TIERS = [
	{"key": "dawn", "label": "晨 06:00", "sun": Color(1.00, 0.80, 0.58), "energy": 0.70,
	 "amb": Color(0.40, 0.44, 0.58), "amb_energy": 0.55},
	{"key": "noon", "label": "午 12:00", "sun": Color(1.00, 0.98, 0.94), "energy": 1.25,
	 "amb": Color(0.62, 0.66, 0.72), "amb_energy": 0.85},
	{"key": "dusk", "label": "昏 20:00", "sun": Color(1.00, 0.55, 0.30), "energy": 0.55,
	 "amb": Color(0.34, 0.28, 0.30), "amb_energy": 0.42},
	{"key": "night", "label": "夜 24:00", "sun": Color(0.48, 0.58, 0.88), "energy": 0.14,
	 "amb": Color(0.10, 0.13, 0.24), "amb_energy": 0.30},
]

const MATRIX_SHADER = """
shader_type canvas_item;
uniform sampler2D screen_tex : hint_screen_texture;
uniform mat3 cb_matrix = mat3(1.0);
void fragment() {
	vec3 c = texture(screen_tex, SCREEN_UV).rgb;
	COLOR = vec4(clamp(cb_matrix * c, 0.0, 1.0), 1.0);
}
"""

var root: Node3D
var layer_bubbles: Node3D
var layer_signals: Node3D
var layer_carry: Node3D
var layer_matrix: CanvasLayer
var sun: DirectionalLight3D
var env: WorldEnvironment
var camera: Camera3D

var _entities = []
var _tripwires = []
var _icon_cache = {}
var _rng := RandomNumberGenerator.new()
var _time := 0.0
var _tier_idx := 1

var _lod_gate_enabled := true
var _lod_deferred_in_cone := 0     # 因锥内而【被推迟】的切换（信息项，闸门生效的证据）
var _lod_executed_in_cone := 0     # 在锥内【实际发生】的切换（判据：必须为 0）
var _lod_missing_carrier := 0      # N1 违规：0–60m 内无载体（判据：必须为 0）
var _lod_band_missing := 0         # 信息项：距离带指定载体缺失（N2 合法推迟会造成计数）
var _vp_h := 1080


# ---------------------------------------------------------------------------
func build(parent: Node3D) -> void:
	root = Node3D.new()
	root.name = "SmokeWorld"
	parent.add_child(root)
	_rng.seed = 20250913
	_build_environment()
	_build_terrain()
	_build_props()
	_build_entities()
	_build_bubbles()
	_build_hudtext()
	_build_signals()
	_build_carry()
	_build_matrix_layer()


# ---------------------------------------------------------------------------
# 环境 / 光照 / 相机
# ---------------------------------------------------------------------------
func _build_environment() -> void:
	env = WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.35, 0.40, 0.46)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.62, 0.66, 0.72)
	e.ambient_light_energy = 0.85
	e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.environment = e
	root.add_child(env)

	sun = DirectionalLight3D.new()
	sun.name = "Sun"
	sun.light_color = Color(1.0, 0.98, 0.94)
	sun.light_energy = 1.25
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 160.0
	sun.rotation_degrees = Vector3(-52.0, 35.0, 0.0)
	root.add_child(sun)

	camera = Camera3D.new()
	camera.name = "SmokeCamera"
	camera.fov = 65.0
	camera.near = 0.1
	camera.far = 1200.0
	camera.position = Vector3(0.0, 9.0, 34.0)
	camera.look_at_from_position(camera.position, Vector3(0.0, 1.5, 0.0))
	root.add_child(camera)


func apply_day_tier(idx: int) -> void:
	_tier_idx = idx % DAY_TIERS.size()
	var t = DAY_TIERS[_tier_idx]
	sun.light_color = t["sun"]
	sun.light_energy = float(t["energy"])
	if env.environment:
		env.environment.ambient_light_color = t["amb"]
		env.environment.ambient_light_energy = float(t["amb_energy"])
		env.environment.background_color = (t["amb"] as Color) * 0.9


func current_tier_label() -> String:
	return String(DAY_TIERS[_tier_idx]["label"])


# ---------------------------------------------------------------------------
# 地形 / 静态垂直元素
# ---------------------------------------------------------------------------
func _build_terrain() -> void:
	var mi := MeshInstance3D.new()
	mi.name = "Terrain"
	var pm := PlaneMesh.new()
	pm.size = Vector2(TERRAIN_SIZE, TERRAIN_SIZE)
	pm.subdivide_width = 48
	pm.subdivide_depth = 48
	mi.mesh = pm
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.32, 0.28, 0.22)
	mat.roughness = 0.95
	mi.set_surface_override_material(0, mat)
	root.add_child(mi)


func _build_props() -> void:
	var parent := Node3D.new()
	parent.name = "Props"
	root.add_child(parent)
	var box := BoxMesh.new()
	box.size = Vector3(1.0, 1.0, 1.0)
	for i in range(PROP_COUNT):
		var mi := MeshInstance3D.new()
		mi.mesh = box
		var a := _rng.randf() * TAU
		var r := 12.0 + _rng.randf() * 180.0
		var h := 2.0 + _rng.randf() * 9.0
		mi.position = Vector3(cos(a) * r, h * 0.5, sin(a) * r)
		mi.scale = Vector3(1.0 + _rng.randf() * 3.0, h, 1.0 + _rng.randf() * 3.0)
		mi.rotation_degrees = Vector3(0.0, _rng.randf() * 360.0, 0.0)
		parent.add_child(mi)


# ---------------------------------------------------------------------------
# 120 个实体（32 full + 88 简模）
# ---------------------------------------------------------------------------
func _build_entities() -> void:
	var parent := Node3D.new()
	parent.name = "Entities"
	root.add_child(parent)
	var capsule := CapsuleMesh.new()
	capsule.radius = 0.35
	capsule.height = 1.7
	var box := BoxMesh.new()
	box.size = Vector3(0.7, 1.7, 0.5)

	for i in range(ENTITY_TOTAL):
		var full := i < ENTITY_FULL
		var body := MeshInstance3D.new()
		body.name = "Actor_%03d" % i
		body.mesh = capsule if full else box
		var m := StandardMaterial3D.new()
		if full:
			m.albedo_color = Color(0.58, 0.48, 0.42)
		else:
			m.albedo_color = Color(0.40, 0.42, 0.45)
		body.set_surface_override_material(0, m)
		var a := _rng.randf() * TAU
		var r := 4.0 + _rng.randf() * 62.0
		body.position = Vector3(cos(a) * r, 0.9, sin(a) * r)
		parent.add_child(body)
		_entities.append({
			"idx": i, "full": full, "body": body,
			"home": body.position,
			"phase": _rng.randf() * TAU,
			"speed": 0.6 + _rng.randf() * 1.1,
			"bubble": null, "bubble_off": Vector3.ZERO,
			"tag": null, "carries": [], "carry_off": [],
			"want_bubble": true, "want_carry": true,
			"vis_bubble": true, "vis_carry": true,
		})


# ---------------------------------------------------------------------------
# 需求气泡（三重编码：形状 glyph / 位置钟位 / 运动）
# ---------------------------------------------------------------------------
func _build_bubbles() -> void:
	layer_bubbles = Node3D.new()
	layer_bubbles.name = "LayerBubbles"
	root.add_child(layer_bubbles)
	var needs = GlyphAtlas.NEED_GLYPHS
	for ent in _entities:
		var glyph: int = int(needs[_rng.randi() % needs.size()])
		var fill := 1 + (_rng.randi() % 3)
		var slot: int = _rng.randi() % 12
		var ang := TAU * float(slot) / 12.0
		var off := Vector3(cos(ang) * 0.55, 1.30, sin(ang) * 0.30)
		var sp := Sprite3D.new()
		sp.name = "Bubble_%03d" % int(ent["idx"])
		sp.texture = _icon(glyph, 1, fill)
		sp.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		sp.pixel_size = 0.010
		layer_bubbles.add_child(sp)
		ent["bubble"] = sp
		ent["bubble_off"] = off


func _icon(glyph: int, chassis: int, fill: int) -> Texture2D:
	var key := "%d_%d_%d" % [glyph, chassis, fill]
	if not _icon_cache.has(key):
		_icon_cache[key] = GlyphAtlas.make_texture(glyph, chassis, fill)
	return _icon_cache[key]


# ---------------------------------------------------------------------------
# 名签（HUD 文本层）
# ---------------------------------------------------------------------------
func _build_hudtext() -> void:
	for ent in _entities:
		var lb := Label3D.new()
		lb.name = "Tag_%03d" % int(ent["idx"])
		lb.text = "拾荒者 %02d" % int(ent["idx"])
		lb.font_size = 28
		lb.modulate = Color(0.95, 0.92, 0.82)
		lb.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		lb.position = Vector3(0.0, 2.05, 0.0)
		(ent["body"] as Node3D).add_child(lb)
		ent["tag"] = lb


# ---------------------------------------------------------------------------
# 4 类 WorldSignal 世界痕迹（4 × 6 = 24 处）
# ---------------------------------------------------------------------------
func _build_signals() -> void:
	layer_signals = Node3D.new()
	layer_signals.name = "LayerSignals"
	root.add_child(layer_signals)
	for k in range(4):
		for s in range(SIGNAL_SITES_PER_KIND):
			var a := _rng.randf() * TAU
			var r := 8.0 + _rng.randf() * 55.0
			var base := Vector3(cos(a) * r, 0.0, sin(a) * r)
			match k:
				0: _sig_reroute(base, s)
				1: _sig_add_watch(base, s)
				2: _sig_deepen_stash(base, s)
				3: _sig_set_trap(base, s)


func _sig_reroute(base: Vector3, s: int) -> void:
	var quad := PlaneMesh.new()
	quad.size = Vector2(0.22, 0.34)
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.18, 0.15, 0.12)
	var holder := Node3D.new()
	holder.name = "SIG_REROUTE_%d" % s
	holder.position = base
	layer_signals.add_child(holder)
	for i in range(12):
		var mi := MeshInstance3D.new()
		mi.mesh = quad
		mi.set_surface_override_material(0, mat)
		var t := float(i) / 11.0
		mi.position = Vector3(sin(t * 3.0) * 0.9, 0.02, t * 7.0)
		mi.rotation_degrees = Vector3(-90.0, float((i % 2) * 180), 0.0)
		holder.add_child(mi)


func _sig_add_watch(base: Vector3, s: int) -> void:
	var holder := Node3D.new()
	holder.name = "SIG_ADD_WATCH_%d" % s
	holder.position = base
	layer_signals.add_child(holder)

	var lamp := OmniLight3D.new()
	lamp.light_color = Color(1.0, 0.78, 0.45)
	lamp.light_energy = 1.6
	lamp.omni_range = 7.0
	lamp.shadow_enabled = false
	lamp.position = Vector3(0.0, 1.9, 0.0)
	holder.add_child(lamp)

	var pm := CylinderMesh.new()
	pm.top_radius = 0.06
	pm.bottom_radius = 0.08
	pm.height = 1.9
	var post := MeshInstance3D.new()
	post.mesh = pm
	post.position = Vector3(0.0, 0.95, 0.0)
	holder.add_child(post)

	var bm := BoxMesh.new()
	bm.size = Vector3(1.1, 0.12, 0.45)
	var seat := MeshInstance3D.new()
	seat.mesh = bm
	seat.position = Vector3(1.1, 0.42, 0.0)
	holder.add_child(seat)

	var cm := CapsuleMesh.new()
	cm.radius = 0.33
	cm.height = 1.6
	var sitter := MeshInstance3D.new()
	sitter.mesh = cm
	sitter.position = Vector3(1.1, 0.95, 0.0)
	holder.add_child(sitter)


func _sig_deepen_stash(base: Vector3, s: int) -> void:
	var holder := Node3D.new()
	holder.name = "SIG_DEEPEN_STASH_%d" % s
	holder.position = base
	layer_signals.add_child(holder)

	var bm := BoxMesh.new()
	bm.size = Vector3(0.34, 0.05, 5.2)
	var groove := MeshInstance3D.new()
	groove.mesh = bm
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.16, 0.13, 0.10)
	groove.set_surface_override_material(0, mat)
	groove.position = Vector3(0.0, 0.025, 2.6)
	holder.add_child(groove)

	var cm := BoxMesh.new()
	cm.size = Vector3(0.9, 1.6, 0.6)
	var cabinet := MeshInstance3D.new()
	cabinet.mesh = cm
	cabinet.position = Vector3(0.0, 0.8, 5.6)
	holder.add_child(cabinet)


func _sig_set_trap(base: Vector3, s: int) -> void:
	var holder := Node3D.new()
	holder.name = "SIG_SET_TRAP_%d" % s
	holder.position = base
	layer_signals.add_child(holder)

	var bm := BoxMesh.new()
	bm.size = Vector3(TRIPWIRE_THICKNESS, TRIPWIRE_THICKNESS, 3.0)
	var wire := MeshInstance3D.new()
	wire.name = "Tripwire"
	wire.mesh = bm
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.85, 0.80, 0.62)
	mat.emission_enabled = true
	mat.emission = Color(0.35, 0.32, 0.22)
	wire.set_surface_override_material(0, mat)
	wire.position = Vector3(0.0, 0.22, 1.5)
	holder.add_child(wire)
	_tripwires.append({"node": wire, "thickness": TRIPWIRE_THICKNESS})

	var sm := SphereMesh.new()
	sm.radius = 0.11
	sm.height = 0.22
	var bell := MeshInstance3D.new()
	bell.mesh = sm
	bell.position = Vector3(0.0, 0.30, 3.05)
	holder.add_child(bell)


# ---------------------------------------------------------------------------
# 携带物剪影（8–25m 唯一的 activity 载体）
# ---------------------------------------------------------------------------
func _build_carry() -> void:
	layer_carry = Node3D.new()
	layer_carry.name = "LayerCarry"
	root.add_child(layer_carry)
	var box := BoxMesh.new()
	box.size = Vector3(0.26, 0.32, 0.26)
	var cyl := CylinderMesh.new()
	cyl.top_radius = 0.16
	cyl.bottom_radius = 0.16
	cyl.height = 0.34
	for ent in _entities:
		var n := 1 + (_rng.randi() % 3)
		var offs = []
		for j in range(n):
			var mi := MeshInstance3D.new()
			mi.mesh = box if (j % 2 == 0) else cyl
			var off := Vector3(0.42 + 0.30 * float(j), 1.05, 0.18)
			layer_carry.add_child(mi)
			offs.append(off)
			ent["carries"].append(mi)
		ent["carry_off"] = offs


# ---------------------------------------------------------------------------
# 色盲 / 高对比色彩矩阵（1 个 fullscreen pass）
# ---------------------------------------------------------------------------
func _build_matrix_layer() -> void:
	layer_matrix = CanvasLayer.new()
	layer_matrix.name = "LayerColorMatrix"
	layer_matrix.layer = 128
	root.add_child(layer_matrix)

	var bbc := BackBufferCopy.new()
	bbc.copy_mode = BackBufferCopy.COPY_MODE_VIEWPORT
	layer_matrix.add_child(bbc)

	var rect := ColorRect.new()
	rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	var sm := ShaderMaterial.new()
	var sh := Shader.new()
	sh.code = MATRIX_SHADER
	sm.shader = sh
	# 红绿色盲模拟矩阵（仅用于制造一个真实 fullscreen pass 的负载）
	sm.set_shader_parameter("cb_matrix", Basis(
		Vector3(0.567, 0.433, 0.000),
		Vector3(0.558, 0.442, 0.000),
		Vector3(0.000, 0.242, 0.758)))
	rect.material = sm
	layer_matrix.add_child(rect)
	layer_matrix.visible = false


# ---------------------------------------------------------------------------
# 各层开关
# ---------------------------------------------------------------------------
func set_bubbles(on: bool) -> void:
	layer_bubbles.visible = on


func set_hudtext(on: bool) -> void:
	for ent in _entities:
		var lb = ent["tag"]
		if lb:
			(lb as Node3D).visible = on


func set_signals(on: bool) -> void:
	layer_signals.visible = on


func set_carry(on: bool) -> void:
	layer_carry.visible = on


func set_color_matrix(on: bool) -> void:
	layer_matrix.visible = on


## 返回 false = 该渲染后端不支持 screen_space_aa（例如 Compatibility/OpenGL3）
func set_fxaa(vp: Viewport, on: bool) -> bool:
	if vp == null:
		return false
	if not ("screen_space_aa" in vp):
		return false
	if not on:
		vp.set("screen_space_aa", 0)
		return true
	vp.set("screen_space_aa", 1)
	return int(vp.get("screen_space_aa")) == 1


func set_lod_gate(on: bool) -> void:
	_lod_gate_enabled = on


# ---------------------------------------------------------------------------
# 每帧更新
# ---------------------------------------------------------------------------
func update(dt: float, entities_move: bool, camera_motion: bool) -> void:
	_time += dt
	for ent in _entities:
		var body: Node3D = ent["body"]
		if entities_move and bool(ent["full"]):
			var home: Vector3 = ent["home"]
			var ph: float = ent["phase"]
			var sp: float = ent["speed"]
			body.position = home + Vector3(
				sin(_time * sp + ph) * 6.0, 0.0, cos(_time * sp * 0.8 + ph) * 6.0)
			body.rotation_degrees.y = rad_to_deg(_time * sp * 0.5 + ph)
		# 运动编码：气泡轻微上下浮动（可降级为静态）
		var sp2: Sprite3D = ent["bubble"]
		if sp2:
			var off: Vector3 = ent["bubble_off"]
			var bob := sin(_time * 2.2 + float(ent["phase"])) * 0.045
			sp2.global_position = body.global_position + off + Vector3(0.0, bob, 0.0)
		var carries = ent["carries"]
		var offs = ent["carry_off"]
		for j in range(carries.size()):
			var cn: Node3D = carries[j]
			cn.global_position = body.global_position + (offs[j] as Vector3)

	if camera_motion:
		_update_camera()
	_update_lod()
	apply_tripwire_clamp(camera)


func _update_camera() -> void:
	var t := _time * 0.35
	var radius := 34.0 + sin(_time * 0.21) * 10.0
	camera.position = Vector3(cos(t) * radius, 9.0 + sin(_time * 0.5) * 2.5, sin(t) * radius)
	camera.look_at_from_position(camera.position, Vector3(0.0, 1.5, 0.0))
	camera.rotate_y(sin(_time * 1.7) * 0.85)


func _update_lod() -> void:
	var cam_pos := camera.global_position
	var fwd := -camera.global_transform.basis.z
	var right := camera.global_transform.basis.x
	var up := camera.global_transform.basis.y

	for ent in _entities:
		var body: Node3D = ent["body"]
		var d := body.global_position.distance_to(cam_pos)
		var want_b := d <= 8.0    # 0–8m：需求气泡
		var want_c := d <= 25.0   # 8–25m：携带物剪影

		# N2：载体切换只允许在可读性保持锥之外（水平 ±25° / 垂直 ±20°）
		var in_cone := false
		var to := (body.global_position - cam_pos).normalized()
		var zc := to.dot(fwd)
		if zc > 0.0:
			var yaw := rad_to_deg(atan2(to.dot(right), zc))
			var pitch := rad_to_deg(atan2(to.dot(up), zc))
			in_cone = abs(yaw) <= CONE_YAW_DEG and abs(pitch) <= CONE_PITCH_DEG

		var pending := (bool(want_b) != bool(ent["vis_bubble"])) \
			or (bool(want_c) != bool(ent["vis_carry"]))

		if pending and in_cone:
			if _lod_gate_enabled:
				# 推迟切换：锥内禁止 pop（保持当前载体不变）
				if not bool(ent.get("pend", false)):
					_lod_deferred_in_cone += 1
					ent["pend"] = true
			else:
				# 对照组：不放闸门就会在锥内 pop
				_lod_executed_in_cone += 1
				ent["vis_bubble"] = want_b
				ent["vis_carry"] = want_c
				ent["pend"] = false
		elif pending:
			ent["vis_bubble"] = want_b
			ent["vis_carry"] = want_c
			ent["pend"] = false
		else:
			ent["pend"] = false

		var sp: Sprite3D = ent["bubble"]
		if sp:
			sp.visible = bool(ent["vis_bubble"])
		for c in ent["carries"]:
			(c as Node3D).visible = bool(ent["vis_carry"])

		# N1（硬判据）：0–60m 内至少有一个可见载体（气泡 / 携带物剪影 / 角色本体剪影）
		var has_carrier := bool(ent["vis_bubble"]) or bool(ent["vis_carry"]) or body.visible
		if d <= 60.0 and not has_carrier:
			_lod_missing_carrier += 1
		# 信息项：距离带「指定」载体缺失（N2 合法推迟会造成少量计数）
		if d <= 8.0 and not bool(ent["vis_bubble"]):
			_lod_band_missing += 1
		elif d <= 25.0 and not bool(ent["vis_carry"]):
			_lod_band_missing += 1


# ---------------------------------------------------------------------------
# 绊线屏幕像素宽度测量（验证 ≥1.5px 最小宽度钳制）
# ---------------------------------------------------------------------------
func set_viewport_height(h: int) -> void:
	_vp_h = h


## 「屏幕空间最小宽度钳制」：把绊线的世界厚度放大到至少 1.5px
## （accessibility.md §7.2：默认 FXAA 会把 1px 绊线糊掉，故必须钳制）
func apply_tripwire_clamp(cam: Camera3D) -> void:
	var half_h := float(_vp_h) * 0.5
	for tw in _tripwires:
		var node: Node3D = tw["node"]
		var d := node.global_position.distance_to(cam.global_position)
		if d < 0.01:
			continue
		var px_per_world := half_h / (d * tan(deg_to_rad(cam.fov) * 0.5))
		if px_per_world <= 0.0:
			continue
		var needed := TRIPWIRE_MIN_PX / px_per_world          # 达到 1.5px 所需的世界厚度
		var t := maxf(TRIPWIRE_THICKNESS, needed)
		tw["clamped"] = t
		tw["px_unclamped"] = TRIPWIRE_THICKNESS * px_per_world
		node.scale = Vector3(t / TRIPWIRE_THICKNESS, t / TRIPWIRE_THICKNESS, 1.0)


func measure_tripwire_px(cam: Camera3D) -> Dictionary:
	var worst := 9999.0
	var samples := 0
	for tw in _tripwires:
		var node: Node3D = tw["node"]
		# 用【钳制后】的厚度：apply_tripwire_clamp() 已把 node.scale 设好
		var thick: float = float(tw.get("clamped", tw["thickness"]))
		var center := node.global_position
		if not cam.is_position_in_frustum(center):
			continue
		var view_dir := (cam.global_position - center).normalized()
		var perp := Vector3(0.0, 0.0, 1.0).cross(view_dir)
		if perp.length_squared() < 1e-6:
			continue
		perp = perp.normalized() * thick * 0.5
		var p0 := cam.unproject_position(center - perp)
		var p1 := cam.unproject_position(center + perp)
		var px := p0.distance_to(p1)
		if px > 0.0:
			worst = minf(worst, px)
			samples += 1
	var unclamped := 9999.0
	for tw in _tripwires:
		if tw.has("px_unclamped"):
			unclamped = minf(unclamped, float(tw["px_unclamped"]))
	if samples == 0:
		return {"px": -1.0, "samples": 0, "px_unclamped": -1.0}
	return {"px": worst, "samples": samples, "px_unclamped": unclamped}


func reset_lod_counters() -> void:
	_lod_deferred_in_cone = 0
	_lod_executed_in_cone = 0
	_lod_missing_carrier = 0
	_lod_band_missing = 0


func lod_violations() -> Dictionary:
	return {
		"deferred_in_cone": _lod_deferred_in_cone,
		"executed_in_cone": _lod_executed_in_cone,
		"missing_carrier": _lod_missing_carrier,
		"band_missing": _lod_band_missing,
	}


func entity_count() -> int:
	return _entities.size()


func full_entity_count() -> int:
	var n := 0
	for e in _entities:
		if bool(e["full"]):
			n += 1
	return n
