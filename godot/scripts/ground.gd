# Ground pass: a painted "trampled" mask (dirt paths from the square to every door and the bridge, worn yards,
# the square itself), a ground material that blends grass and dirt by it, and grass clumps scattered on the grass,
# densest along path edges and walls where real grass grows tall.
extends RefCounted

const MASK_PX := 512
const MASK_RECT := Rect2(-40, -40, 80, 80)  # world x/z covered by the mask
const Look = preload("res://scripts/look.gd")

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
	_debris(root, houses, river_dist, river_half, rng)
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
	Look.apply(m, Look.v("materials.ground"))
	m.set_shader_parameter("mask_rect", Vector4(MASK_RECT.position.x, MASK_RECT.position.y, MASK_RECT.size.x, MASK_RECT.size.y))
	return m

# Grass tufts (art review 2026-10-16): several low variants of many fine blades, root colour = the ground's, normals
# forced up so tufts light like the ground they grow from, clustered by noise. Reads as texture at the far view.
const TUFT_VARIANTS := 5

func _tuft_mesh(rng: RandomNumberGenerator) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	st.set_normal(Vector3.UP)
	var tuft: Dictionary = Look.v("placement.grass_tuft")
	var blades := rng.randi_range(tuft.blades[0], tuft.blades[1])
	var dry := rng.randf() * 0.4
	for i in blades:
		var a := rng.randf() * TAU
		var h := rng.randf_range(tuft.height[0], tuft.height[1])
		var lean := Vector3(cos(a), 0, sin(a)) * rng.randf_range(0.02, 0.12)
		var side := Vector3(-sin(a), 0, cos(a)) * 0.012
		var base := Vector3(cos(a), 0, sin(a)) * rng.randf_range(0.0, 0.15)
		# tufts take the ground's own darkening (ground_value) so they never read as bright sprinkles (review 4)
		var gv: float = Look.v("materials.ground").ground_value
		var root_c := Look.to_color(tuft.root) * gv
		var tip_c := Look.to_color(tuft.tip).lerp(Look.to_color(tuft.tip_dry), dry + rng.randf() * 0.2) * gv
		# both windings with the UP normal and back-face culling: with culling off, Godot flips the normal on back faces,
		# which then point down and render near-black (the "dark starbursts", reviews 3-4)
		var tri := [[base - side, root_c], [base + side, root_c], [base + lean + Vector3(0, h, 0), tip_c]]
		for order in [[0, 1, 2], [0, 2, 1]]:
			for k in order:
				st.set_color(tri[k][1])
				st.add_vertex(tri[k][0])
	return st.commit()

func _grass(root: Node3D, houses: Array, river_dist: Callable, river_half: float, rng: RandomNumberGenerator) -> void:
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.cull_mode = BaseMaterial3D.CULL_BACK  # see _tuft_mesh: both windings are emitted
	mat.roughness = 0.95
	mat.vertex_color_is_srgb = true  # tuft colours are authored as sRGB; read as linear they rendered washed-out white
	var noise := FastNoiseLite.new()
	noise.seed = 3
	noise.frequency = 0.12
	var xforms := []
	for v in TUFT_VARIANTS:
		xforms.append([])
	for i in int(Look.v("placement.grass_points")):
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
		var keep := clampf(Look.v("placement.grass_keep") + noise.get_noise_2d(p.x, p.y) * 1.2, 0.0, 1.0)  # clusters and bare patches
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
		if not Look.v("placement.grass_tuft_shadows"):
			mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		root.add_child(mmi)

# Ground debris (art direction round 3): pebbles, twigs, wood chips and dead leaves, one small "detail event" per
# ~1 m², concentrated where real yards collect them: path edges, around houses, under the oaks. Tiny, low-poly,
# coloured from the ground palette so they read as texture at the far view and as objects at the mid view.

func _pebble_mesh(rng: RandomNumberGenerator) -> Mesh:
	var s := SphereMesh.new()
	s.radius = 0.05
	s.height = 0.05
	s.radial_segments = 6
	s.rings = 3
	var st := SurfaceTool.new()
	st.create_from(s, 0)
	var arr := st.commit_to_arrays()
	var verts: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
	for i in verts.size():  # lumpy, flattened
		verts[i] = verts[i] * Vector3(rng.randf_range(0.7, 1.3), 0.6, rng.randf_range(0.7, 1.3))
	arr[Mesh.ARRAY_VERTEX] = verts
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	return m

func _twig_mesh(rng: RandomNumberGenerator) -> Mesh:
	var b := BoxMesh.new()
	b.size = Vector3(rng.randf_range(0.15, 0.35), 0.015, 0.02)
	return b

func _chip_mesh(rng: RandomNumberGenerator) -> Mesh:
	var b := BoxMesh.new()
	b.size = Vector3(rng.randf_range(0.04, 0.09), 0.01, rng.randf_range(0.02, 0.04))
	return b

func _leaf_mesh(rng: RandomNumberGenerator) -> Mesh:
	var q := QuadMesh.new()
	q.size = Vector2(0.07, 0.05)
	q.orientation = PlaneMesh.FACE_Y
	return q

func _debris_color(kind: String) -> Color:
	return Look.to_color(Look.v("placement.debris_colors")[kind]) * float(Look.v("materials.ground").ground_value)

func _debris_mat(c: Color, rough := 0.9) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	return m

func _debris(root: Node3D, houses: Array, river_dist: Callable, river_half: float, rng: RandomNumberGenerator) -> void:
	var kinds := [
		# [mesh factory, material, variants, weight on paths, weight on grass, weight near walls]
		[_pebble_mesh, _debris_mat(_debris_color("pebble")), 4, 0.55, 0.1, 0.35],
		[_twig_mesh, _debris_mat(_debris_color("twig")), 2, 0.1, 0.3, 0.25],
		[_chip_mesh, _debris_mat(_debris_color("chip")), 2, 0.2, 0.05, 0.3],
		[_leaf_mesh, _debris_mat(_debris_color("leaf")), 1, 0.15, 0.55, 0.1],
	]
	var buckets := []
	for k in kinds:
		var per := []
		for v in k[2]:
			per.append([])
		buckets.append(per)
	var noise := FastNoiseLite.new()
	noise.seed = 11
	noise.frequency = 0.2
	for i in int(Look.v("placement.debris_points")):
		var p := Vector2(rng.randf_range(-30, 30), rng.randf_range(-30, 30))
		if river_dist.call(p) < river_half + 0.2:
			continue
		var near_wall := false
		for h in houses:
			var d := Vector2(p.x - h[0], p.y - h[1]).length()
			if d < 2.6:
				near_wall = false  # inside the footprint
				break
			near_wall = near_wall or d < 5.5
		var dirt := dirt_at(p)
		var edge := 1.0 - absf(dirt - 0.45) * 2.5  # strongest on the fringe of paths and yards
		var density := clampf(0.25 + 0.6 * maxf(edge, 0.0) + (0.35 if near_wall else 0.0) + noise.get_noise_2d(p.x, p.y) * 0.4, 0.0, 1.0)
		if rng.randf() > density:
			continue
		var w := []
		for k in kinds:
			w.append(k[5] if near_wall else (k[3] if dirt > 0.4 else k[4]))
		var r: float = rng.randf() * (w[0] + w[1] + w[2] + w[3])
		var ki := 0
		while ki < 3 and r > w[ki]:
			r -= w[ki]
			ki += 1
		var basis := Basis(Vector3.UP, rng.randf() * TAU).scaled(Vector3.ONE * rng.randf_range(0.6, 1.5))
		buckets[ki][rng.randi() % kinds[ki][2]].append(Transform3D(basis, Vector3(p.x, 0.005, p.y)))
	for ki in kinds.size():
		for v in kinds[ki][2]:
			var list: Array = buckets[ki][v]
			if list.is_empty():
				continue
			var mm := MultiMesh.new()
			mm.transform_format = MultiMesh.TRANSFORM_3D
			mm.mesh = kinds[ki][0].call(rng)
			mm.instance_count = list.size()
			for i in list.size():
				mm.set_instance_transform(i, list[i])
			var mmi := MultiMeshInstance3D.new()
			mmi.name = "Debris%d_%d" % [ki, v]
			mmi.multimesh = mm
			mmi.material_override = kinds[ki][1]
			mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF  # too small to matter; saves the shadow pass
			root.add_child(mmi)
