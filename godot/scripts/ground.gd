# Ground pass: a painted "trampled" mask (dirt paths from the square to every door and the bridge, worn yards,
# the square itself), a ground material that blends grass and dirt by it, and grass clumps scattered on the grass,
# densest along path edges and walls where real grass grows tall.
extends RefCounted

const MASK_PX := 512
const MASK_RECT := Rect2(-40, -40, 80, 80)  # world x/z covered by the mask
const GRASS_CLUMPS := 36000  # candidate points; noise clustering keeps roughly a third

var _mask := Image.create(MASK_PX, MASK_PX, false, Image.FORMAT_RF)

# houses: [[x, z, yaw_deg, variant], ...]; door is on the house's local +Z side.
func build(root: Node3D, houses: Array, square: Vector2, bridge: Vector2, river_dist: Callable, river_half: float,
		rng: RandomNumberGenerator, grass_mat: Material) -> Material:
	_disc(square, 4.5, 1.0)
	for h in houses:
		var door := _door(h)
		_line(square, door, 0.9, 0.95)
		_disc(door, 1.6, 0.8)
	_line(square, bridge, 1.0, 0.95)
	_line(bridge + Vector2(0, -11), bridge + Vector2(0, -17), 0.9, 0.9)  # path continues on the far bank
	var mat := _material(grass_mat)
	_grass(root, houses, river_dist, river_half, rng)
	return mat

func _door(h: Array) -> Vector2:
	var local := Vector2(0, 3.2)  # just outside the door
	return Vector2(h[0], h[1]) + local.rotated(-deg_to_rad(h[2]))

func _px(p: Vector2) -> Vector2:
	return (p - MASK_RECT.position) / MASK_RECT.size * MASK_PX

func _stamp(center: Vector2, radius: float, strength: float) -> void:
	var c := _px(center)
	var r := radius / MASK_RECT.size.x * MASK_PX
	for y in range(maxi(0, int(c.y - r)), mini(MASK_PX, int(c.y + r) + 1)):
		for x in range(maxi(0, int(c.x - r)), mini(MASK_PX, int(c.x + r) + 1)):
			var d := Vector2(x, y).distance_to(c) / r
			if d < 1.0:
				var v := strength * smoothstep(1.0, 0.45, d)
				_mask.set_pixel(x, y, Color(maxf(_mask.get_pixel(x, y).r, v), 0, 0))

func _disc(center: Vector2, radius: float, strength: float) -> void:
	_stamp(center, radius, strength)

func _line(a: Vector2, b: Vector2, half_width: float, strength: float) -> void:
	var steps := int(a.distance_to(b) / (half_width * 0.5)) + 1
	for i in steps + 1:
		var p := a.lerp(b, float(i) / steps)
		p += Vector2(sin(p.y * 0.7), cos(p.x * 0.6)) * 0.25  # paths wander slightly
		_stamp(p, half_width, strength)

func dirt_at(p: Vector2) -> float:
	var q := _px(p)
	if q.x < 0 or q.y < 0 or q.x >= MASK_PX or q.y >= MASK_PX:
		return 0.0
	return _mask.get_pixel(int(q.x), int(q.y)).r

func _material(grass_mat: StandardMaterial3D) -> ShaderMaterial:
	var dirt := "res://assets/textures/riverbed/"
	var m := ShaderMaterial.new()
	m.shader = load("res://shaders/ground.gdshader")
	m.set_shader_parameter("grass_albedo", grass_mat.albedo_texture)
	m.set_shader_parameter("grass_normal", grass_mat.normal_texture)
	m.set_shader_parameter("grass_rough", grass_mat.roughness_texture)
	m.set_shader_parameter("dirt_albedo", load(dirt + "albedo.jpg"))
	m.set_shader_parameter("dirt_normal", load(dirt + "normal.jpg"))
	m.set_shader_parameter("dirt_rough", load(dirt + "roughness.jpg"))
	m.set_shader_parameter("mask", ImageTexture.create_from_image(_mask))
	m.set_shader_parameter("mask_rect", Vector4(MASK_RECT.position.x, MASK_RECT.position.y, MASK_RECT.size.x, MASK_RECT.size.y))
	return m

# Grass tufts (art review 2026-10-16): several low variants of many fine blades, root colour = the ground's, normals
# forced up so tufts light like the ground they grow from, clustered by noise. Reads as texture at the far view.
const TUFT_VARIANTS := 5

func _tuft_mesh(rng: RandomNumberGenerator) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	st.set_normal(Vector3.UP)
	var blades := rng.randi_range(15, 25)
	var dry := rng.randf() * 0.4
	for i in blades:
		var a := rng.randf() * TAU
		var h := rng.randf_range(0.12, 0.28)
		var lean := Vector3(cos(a), 0, sin(a)) * rng.randf_range(0.02, 0.12)
		var side := Vector3(-sin(a), 0, cos(a)) * 0.012
		var base := Vector3(cos(a), 0, sin(a)) * rng.randf_range(0.0, 0.09)
		var root_c := Color(0.17, 0.2, 0.09)  # ~ the tinted grass ground albedo, so tufts grow out of it instead of sitting on it
		var tip_c := Color(0.26, 0.31, 0.13).lerp(Color(0.38, 0.33, 0.17), dry + rng.randf() * 0.2)
		for v in [[base - side, root_c], [base + side, root_c], [base + lean + Vector3(0, h, 0), tip_c]]:
			st.set_color(v[1])
			st.add_vertex(v[0])
	return st.commit()

func _grass(root: Node3D, houses: Array, river_dist: Callable, river_half: float, rng: RandomNumberGenerator) -> void:
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.roughness = 0.95
	var noise := FastNoiseLite.new()
	noise.seed = 3
	noise.frequency = 0.12
	var xforms := []
	for v in TUFT_VARIANTS:
		xforms.append([])
	for i in GRASS_CLUMPS:
		var p := Vector2(rng.randf_range(-34, 34), rng.randf_range(-34, 34))
		if river_dist.call(p) < river_half + 1.8:  # bare sand along the banks
			continue
		var near_house := false
		for h in houses:
			near_house = near_house or Vector2(p.x - h[0], p.y - h[1]).length() < 3.6
		if near_house:
			continue
		var d := dirt_at(p)
		if d > 0.3:
			continue  # dead zone: no tufts on paths and their worn fringe
		var keep := clampf(0.15 + noise.get_noise_2d(p.x, p.y) * 1.2, 0.0, 1.0)  # clusters and bare patches
		if rng.randf() > keep:
			continue
		var s := rng.randf_range(0.6, 1.3)
		xforms[rng.randi() % TUFT_VARIANTS].append(Transform3D(Basis(Vector3.UP, rng.randf() * TAU).scaled(Vector3.ONE * s), Vector3(p.x, 0, p.y)))
	for v in TUFT_VARIANTS:
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.mesh = _tuft_mesh(rng)
		mm.instance_count = xforms[v].size()
		for i in xforms[v].size():
			mm.set_instance_transform(i, xforms[v][i])
		var mmi := MultiMeshInstance3D.new()
		mmi.name = "GrassTufts%d" % v
		mmi.multimesh = mm
		mmi.material_override = mat
		root.add_child(mmi)
