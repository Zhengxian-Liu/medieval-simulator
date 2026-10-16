# Placeholder test village (metres, Y up): houses, river, bridge, torch, barrels, fences, rocks, trees.
# Flat-coloured on purpose; real textures and models replace these materials/meshes later.
extends RefCounted

const RIVER_Z := -14.0
const RIVER_HALF := 3.5  # shore-to-shore half width
const BED_DEPTH := 1.5

const COLORS := {
	grass = Color("4a5a2a"), stone = Color("6b6862"), plaster = Color("cfc2a6"),
	timber = Color("4a3220"), thatch = Color("8a6d3b"), bark = Color("3d2b1c"),
	leaves = Color("2f4a22"), iron = Color("222222"), cloth = Color("7a2e24"), riverbed = Color("4d4434"),
}

var _rng := RandomNumberGenerator.new()
var _mats := {}

# Slots with a Poly Haven texture set (see assets/CREDITS.md); others stay flat colour placeholders.
const TEXTURED := {grass = 0.25, riverbed = 0.4, stone = 0.6, plaster = 0.5, timber = 0.6, thatch = 0.5, rock = 0.8}  # tiles per metre

# leafy_grass reads as dry dirt from above; push it toward green.
const TINT := {grass = Color(0.5, 0.66, 0.42)}

func _mat(name: String) -> StandardMaterial3D:
	if not _mats.has(name):
		var m := StandardMaterial3D.new()
		m.roughness = 0.9
		m.cull_mode = BaseMaterial3D.CULL_DISABLED  # riverbed is a hand-built strip; avoid winding surprises
		var slot := "rock" if name == "stone_rock" else name
		if TEXTURED.has(slot):
			var dir := "res://assets/textures/%s/" % slot
			m.albedo_texture = load(dir + "albedo.jpg")
			m.normal_enabled = true
			m.normal_texture = load(dir + "normal.jpg")
			m.roughness_texture = load(dir + "roughness.jpg")
			m.roughness = 1.0
			# World-space triplanar: primitive meshes have no sensible UVs, and tiling stays the same size everywhere.
			m.uv1_triplanar = true
			m.uv1_world_triplanar = true
			m.uv1_scale = Vector3.ONE * TEXTURED[slot]
			m.albedo_color = TINT.get(slot, Color.WHITE)
		else:
			m.albedo_color = COLORS[name]
		_mats[name] = m
	return _mats[name]

func _add(parent: Node3D, mesh: Mesh, mat: String, pos: Vector3, rot_y_deg := 0.0) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = _mat(mat)
	mi.position = pos
	mi.rotation_degrees.y = rot_y_deg
	parent.add_child(mi)
	return mi

func _box(size: Vector3) -> BoxMesh:
	var b := BoxMesh.new()
	b.size = size
	return b

func _cyl(top: float, bottom: float, height: float, sides := 8) -> CylinderMesh:
	var c := CylinderMesh.new()
	c.top_radius = top
	c.bottom_radius = bottom
	c.height = height
	c.radial_segments = sides
	return c

func build(root: Node3D) -> Node3D:
	_rng.seed = 7  # deterministic so screenshots are comparable between runs
	_ground(root)
	_river(root)
	_add(root, _box(Vector3(2.4, 0.25, 9.0)), "timber", Vector3(3, 0.35, RIVER_Z))  # bridge

	var houses := [[-9, 6, 10, "plaster"], [6, 9, -15, "timber"], [-3, 19, 80, "plaster"], [15, -3, 40, "plaster"]]
	for h in houses:
		_house(root, Vector3(h[0], 0, h[1]), h[2], h[3])

	for i in 5:
		_add(root, _cyl(0.32, 0.35, 0.9, 12), "timber", Vector3(-4 + (i % 3) * 0.75, 0.45, 9.5 + (i / 3) * 0.75))
	_stall(root, Vector3(4, 0, 2))
	_fence(root, Vector3(-14, 0, 12), 8, 0)
	_fence(root, Vector3(10, 0, 14), 6, 90)

	_add(root, _cyl(0.05, 0.07, 1.6, 6), "iron", Vector3(1.5, 0.8, 1.5))
	var torch := OmniLight3D.new()
	torch.light_color = Color(1.0, 0.6, 0.25)
	torch.light_energy = 2.0
	torch.omni_range = 8.0
	torch.shadow_enabled = true
	torch.position = Vector3(1.5, 1.75, 1.5)
	root.add_child(torch)

	# Trees: a ring inside the far view plus a denser belt behind the village.
	for i in 40:
		var a := _rng.randf() * TAU
		var dist := (8.0 + _rng.randf() * 10.0) if i < 14 else (20.0 + _rng.randf() * 30.0)
		var p := Vector3(cos(a) * dist, 0, sin(a) * dist + 4.0)
		var clear := absf(p.z - RIVER_Z) > RIVER_HALF + 1.5
		for h in houses:
			clear = clear and Vector2(p.x - h[0], p.z - h[1]).length() > 5.5  # keep trunks and crowns out of houses
		if clear:
			_tree(root, p)
	for i in 50:
		var along := i < 28
		var p := Vector3(_rng.randf_range(-30, 30), 0.05, RIVER_Z + signf(_rng.randf() - 0.5) * _rng.randf_range(RIVER_HALF - 0.4, RIVER_HALF + 1.5)) if along \
			else Vector3(_rng.randf_range(-22, 22), 0.05, _rng.randf_range(-6, 26))
		_rock(root, p)

	var player := _add(root, CapsuleMesh.new(), "cloth", Vector3(1, 1.0, 4))
	(player.mesh as CapsuleMesh).radius = 0.3
	(player.mesh as CapsuleMesh).height = 1.7
	player.position.y = 0.85
	return player

func _ground(root: Node3D) -> void:
	var far_edge := 150.0
	var north := RIVER_Z - RIVER_HALF
	var south := RIVER_Z + RIVER_HALF
	_add(root, _box(Vector3(300, 1, far_edge + north)), "grass", Vector3(0, -0.5, (north - far_edge) / 2.0))
	_add(root, _box(Vector3(300, 1, far_edge - south)), "grass", Vector3(0, -0.5, (south + far_edge) / 2.0))

# Channel cross-section: shore (y=0) slopes down to a flat bed, so the water shader sees a depth gradient for foam.
func _river(root: Node3D) -> void:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var profile := [Vector2(-RIVER_HALF, 0.0), Vector2(-1.5, -BED_DEPTH), Vector2(1.5, -BED_DEPTH), Vector2(RIVER_HALF, 0.0)]
	for s in profile.size() - 1:
		var a: Vector2 = profile[s]
		var b: Vector2 = profile[s + 1]
		var v := [Vector3(-150, a.y, RIVER_Z + a.x), Vector3(150, a.y, RIVER_Z + a.x), Vector3(150, b.y, RIVER_Z + b.x), Vector3(-150, b.y, RIVER_Z + b.x)]
		for idx in [0, 2, 1, 0, 3, 2]:
			st.add_vertex(v[idx])
	st.generate_normals()
	_add(root, st.commit(), "riverbed", Vector3.ZERO)

	var surface := PlaneMesh.new()
	surface.size = Vector2(300, RIVER_HALF * 2.0)
	var water := MeshInstance3D.new()
	water.mesh = surface
	var sm := ShaderMaterial.new()
	sm.shader = load("res://shaders/water.gdshader")
	water.material_override = sm
	water.position = Vector3(0, -0.12, RIVER_Z)
	water.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	root.add_child(water)

func _house(root: Node3D, pos: Vector3, yaw: float, wall: String) -> void:
	var h := Node3D.new()
	h.position = pos
	h.rotation_degrees.y = yaw
	root.add_child(h)
	var w := 6.0
	var d := 4.5
	var ht := 3.2
	_add(h, _box(Vector3(w + 0.3, 0.6, d + 0.3)), "stone", Vector3(0, 0.3, 0))
	_add(h, _box(Vector3(w, ht, d)), wall, Vector3(0, 0.6 + ht / 2.0, 0))
	# Closed gable roof: prism ridge along X with eaves overhang.
	var roof := PrismMesh.new()
	roof.size = Vector3(d + 0.8, d * 0.55, w + 0.6)
	_add(h, roof, "thatch", Vector3(0, 0.6 + ht + d * 0.275, 0), 90)
	_add(h, _box(Vector3(0.9, 1.8, 0.08)), "timber", Vector3(0, 1.5, d / 2.0 + 0.04))

func _stall(root: Node3D, pos: Vector3) -> void:
	_add(root, _box(Vector3(2.4, 0.08, 1.4)), "cloth", pos + Vector3(0, 2.2, 0))
	for c in [Vector2(-1.1, -0.6), Vector2(1.1, -0.6), Vector2(-1.1, 0.6), Vector2(1.1, 0.6)]:
		_add(root, _cyl(0.05, 0.05, 2.2, 6), "timber", pos + Vector3(c.x, 1.1, c.y))

func _fence(root: Node3D, start: Vector3, posts: int, yaw: float) -> void:
	var dir := Vector3.RIGHT.rotated(Vector3.UP, deg_to_rad(yaw))
	for i in posts:
		_add(root, _cyl(0.06, 0.07, 1.1, 6), "timber", start + dir * i * 1.5 + Vector3(0, 0.55, 0))
	for y in [0.45, 0.85]:
		_add(root, _box(Vector3((posts - 1) * 1.5, 0.08, 0.06)), "timber", start + dir * (posts - 1) * 0.75 + Vector3(0, y, 0), yaw)

func _tree(root: Node3D, pos: Vector3) -> void:
	var h := 3.5 + _rng.randf() * 3.0
	_add(root, _cyl(0.14, 0.2, h * 0.4), "bark", pos + Vector3(0, h * 0.2, 0))
	_add(root, _cyl(0.0, 1.1 + _rng.randf() * 0.6, h * 0.8, 9), "leaves", pos + Vector3(0, h * 0.8, 0))

func _rock(root: Node3D, pos: Vector3) -> void:
	var s := SphereMesh.new()
	var r := 0.3 + _rng.randf() * 0.7
	s.radius = r
	s.height = r * 1.2
	s.radial_segments = 6
	s.rings = 3
	var mi := _add(root, s, "stone_rock", pos, _rng.randf() * 360.0)
	mi.scale = Vector3(1.0, 1.0, 0.7 + _rng.randf() * 0.5)
