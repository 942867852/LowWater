# ============================================================================
# scripts/glyph_atlas.gd · 程序化生成「全局视觉字母表」图标（零美术资产依赖）
# ----------------------------------------------------------------------------
# 对应 docs/architecture/accessibility.md §2：
#   · 形状 glyph 闭集 10 个（水滴/碗/月牙/竖菱形/双扣环/盾/火焰/排夹/十字/芽）
#   · 底盘 3 值：0=无（一个人）/ 1=圆形（需求/状态）/ 2=方形（物资/簇）
#   · 填充 3 档：1=空心描边 / 2=半填充 / 3=实心+加粗外描边
# 本包用 32×32 程序化图标代替真实美术资产；目的只是制造等价的渲染负载与
# 可判读性，不追求美术质量。
# ============================================================================
extends RefCounted

const CELL := 32
const GLYPH_NAMES: Array[String] = [
	"水滴", "碗", "月牙", "竖菱形", "双扣环",
	"盾", "火焰", "排夹", "十字", "芽",
]
# 需求侧（圆形底盘）用到的 glyph；物资侧（方形底盘）用到的 glyph
const NEED_GLYPHS := [0, 1, 2, 3, 4, 5]      # 渴/饿/困/怕/社交/信条
const SUPPLY_GLYPHS := [0, 1, 3, 6, 7, 8, 9]  # 水/食/弹/燃料/弹(排夹)/药/种


# ---------------------------------------------------------------------------
# 对外：造一张 32×32 RGBA 图标
#   glyph   0..9
#   chassis 0=无底盘 1=圆形 2=方形
#   fill    1=空心描边 2=半填充 3=实心+加粗外描边
# ---------------------------------------------------------------------------
static func render(glyph: int, chassis: int, fill: int) -> Image:
	var m := _build_mask(glyph)
	var img := Image.create(CELL, CELL, false, Image.FORMAT_RGBA8)
	img.fill(Color(0.0, 0.0, 0.0, 0.0))

	# --- 底盘 ---
	if chassis != 0:
		var plate_col := Color(0.06, 0.06, 0.07, 0.88)       # 近黑底，保证 WCAG 对比
		var bold := (fill == 3)
		for y in range(CELL):
			for x in range(CELL):
				var inside := _chassis_point(chassis, x, y)
				if not inside:
					continue
				var edge := _chassis_is_edge(chassis, x, y)
				if bold and edge:
					img.set_pixel(x, y, Color(1.0, 1.0, 1.0, 1.0))  # 加粗外描边
				else:
					img.set_pixel(x, y, plate_col)

	# --- 形状 glyph ---
	var fg := Color(1.0, 0.96, 0.86, 1.0)                    # 暖白，模拟废土纸色
	for y in range(CELL):
		for x in range(CELL):
			if not m[y * CELL + x]:
				continue
			var draw := false
			if fill == 1:
				draw = _mask_is_outline(m, x, y)               # 空心描边
			elif fill == 2:
				draw = (y >= CELL / 2) or _mask_is_outline(m, x, y)  # 半填充（下半）
			else:
				draw = true                                    # 实心
			if draw:
				img.set_pixel(x, y, fg)
	return img


static func make_texture(glyph: int, chassis: int, fill: int) -> ImageTexture:
	return ImageTexture.create_from_image(render(glyph, chassis, fill))


# ---------------------------------------------------------------------------
# 形状 mask（每 glyph 一个解析式，保证 16×16 仍可辨 → 32×32 更稳）
# ---------------------------------------------------------------------------
static func _build_mask(glyph: int) -> PackedByteArray:
	var m := PackedByteArray()
	m.resize(CELL * CELL)
	for y in range(CELL):
		for x in range(CELL):
			m[y * CELL + x] = 1 if _in_glyph(glyph, x, y) else 0
	return m


static func _in_glyph(g: int, x: int, y: int) -> bool:
	var fx := float(x - 15) / 15.0
	var fy := float(y - 15) / 15.0
	match g:
		0:  # 水滴：上尖下圆
			var cy := fy - 0.25
			if fx * fx + cy * cy <= 0.40:
				return true
			if fy <= 0.05 and fy >= -0.85:
				return abs(fx) <= 0.30 * (fy + 0.85) / 0.90
			return false
		1:  # 碗：开口向上的半圆 + 口沿横线
			if abs(fy + 0.12) <= 0.07 and abs(fx) <= 0.72:
				return true
			var by := fy + 0.12
			if by >= 0.0 and fx * fx + by * by <= 0.50:
				return true
			return false
		2:  # 月牙：横躺的闭合弧（大圆减小圆）
			var d1 := fx * fx + fy * fy
			var dx := fx + 0.32
			return d1 <= 0.62 and (dx * dx + fy * fy) > 0.40
		3:  # 竖菱形：上下尖、左右窄
			return abs(fx) / 0.44 + abs(fy) / 0.88 <= 1.0
		4:  # 双扣环：两个相扣圆环
			var a := (fx + 0.26) * (fx + 0.26) + fy * fy
			var b := (fx - 0.26) * (fx - 0.26) + fy * fy
			return (a <= 0.34 and a >= 0.15) or (b <= 0.34 and b >= 0.15)
		5:  # 盾：上宽下尖六边形
			if fy < -0.32:
				return abs(fx) <= 0.64
			var t := (fy + 0.32) / 1.32
			return abs(fx) <= 0.64 * (1.0 - t)
		6:  # 火焰：带缺口的竖三角
			if fy > 0.86 or fy < -0.86:
				return false
			var halfw := 0.60 * (0.86 - fy) / 1.72
			if abs(fx) > halfw:
				return false
			if fy > -0.10 and fy < 0.40 and fx > 0.08 and fx < 0.30:
				return false  # 缺口
			return true
		7:  # 排夹：方括号形 + 底横线
			if abs(fy + 0.56) <= 0.08 and abs(fx) <= 0.60:
				return true
			if abs(fx) >= 0.42 and abs(fx) <= 0.60 and fy >= -0.62 and fy <= 0.30:
				return true
			if abs(fy - 0.30) <= 0.08 and abs(fx) <= 0.62:
				return true
			return false
		8:  # 十字：等臂正十字
			return (abs(fx) <= 0.19 and abs(fy) <= 0.82) or (abs(fy) <= 0.19 and abs(fx) <= 0.82)
		9:  # 芽：小圆 + 一道向上的短芽
			if fx * fx + (fy - 0.18) * (fy - 0.18) <= 0.30:
				return true
			return abs(fx) <= 0.11 and fy <= 0.18 and fy >= -0.78
	return false


static func _mask_is_outline(m: PackedByteArray, x: int, y: int) -> bool:
	if x > 0 and m[y * CELL + x - 1] == 0:
		return true
	if x < CELL - 1 and m[y * CELL + x + 1] == 0:
		return true
	if y > 0 and m[(y - 1) * CELL + x] == 0:
		return true
	if y < CELL - 1 and m[(y + 1) * CELL + x] == 0:
		return true
	return false


# ---------------------------------------------------------------------------
# 底盘
# ---------------------------------------------------------------------------
static func _chassis_point(chassis: int, x: int, y: int) -> bool:
	if chassis == 1:
		var dx := float(x - 16) / 14.5
		var dy := float(y - 16) / 14.5
		return dx * dx + dy * dy <= 1.0
	elif chassis == 2:
		return x >= 2 and x <= 29 and y >= 3 and y <= 29
	return false


static func _chassis_is_edge(chassis: int, x: int, y: int) -> bool:
	if chassis == 1:
		var dx := float(x - 16) / 14.5
		var dy := float(y - 16) / 14.5
		var r := sqrt(dx * dx + dy * dy)
		return r > 0.82
	elif chassis == 2:
		return x <= 4 or x >= 27 or y <= 5 or y >= 27
	return false
