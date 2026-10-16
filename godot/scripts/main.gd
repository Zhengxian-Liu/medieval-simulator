# Entry scene: builds the village, lighting with a time of day, and the iso-to-mid zoom camera.
# Mouse wheel zooms, WASD moves (screen-relative), T advances the clock by one hour.
# `godot --path godot -- --capture=<abs dir>` renders far/mid/dusk shots at 1920x1080 into <dir> and quits.
extends Node3D

const CameraZoom = preload("res://scripts/camera_zoom.gd")
const Look = preload("res://scripts/look.gd")
const Village = preload("res://scripts/village.gd")

const ZOOM_STEP := 0.08
const ZOOM_SPEED := 6.0
const MOVE_SPEED := 4.0
const SHOT_SIZE := Vector2i(1920, 1080)
const SUPERSAMPLE := 2  # render shots at 2x and downscale: clean edges on thin geometry (grass, fences, beams)
const SETTLE_FRAMES := 90  # SDFGI and SSAO converge over several frames

var zoom := 0.0
var zoom_target := 0.0
var hour := 10.0

var player: Node3D
var sun: DirectionalLight3D
var moon: DirectionalLight3D
var environment: Environment
var sky_mat: ShaderMaterial
var camera: Camera3D
var village: RefCounted

func _ready() -> void:
	village = Village.new()
	player = village.build(self)
	_build_lighting()
	var export_path := _user_arg("--export-gltf=")
	if not export_path.is_empty():
		_export_gltf(export_path)
		return
	var capture_dir := _user_arg("--capture=")
	if capture_dir.is_empty():
		camera = Camera3D.new()
		add_child(camera)
	else:
		var vp := SubViewport.new()  # fixed size regardless of the screen the window lands on
		vp.size = SHOT_SIZE * SUPERSAMPLE
		vp.msaa_3d = Viewport.MSAA_4X
		vp.use_taa = true
		vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		add_child(vp)
		camera = Camera3D.new()
		vp.add_child(camera)
		_capture(vp, capture_dir)
	camera.current = true
	_apply_camera()
	_apply_time()

func _user_arg(prefix: String) -> String:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with(prefix):
			return arg.trim_prefix(prefix)
	return ""

# Writes the village, the sun and the far-iso camera to a .glb so Blender can render the same shot offline.
func _export_gltf(path: String) -> void:
	hour = 11.0
	_apply_time()
	for shot in [["FarIsoCamera", 0.0], ["MidCamera", 1.0]]:  # same poses as the far and mid captures
		camera = Camera3D.new()
		camera.name = shot[0]
		add_child(camera)
		zoom = shot[1]
		_apply_camera()
	# ShaderMaterials do not survive glTF; Blender rebuilds the ground from this mask (tools/render_cycles.py)
	var mask: Image = village._ground._mask.duplicate()
	mask.convert(Image.FORMAT_L8)
	mask.save_png(path.get_basename() + "_mask.png")
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var err := doc.append_from_scene(self, state)
	if err == OK:
		err = doc.write_to_filesystem(state, path)
	print("export_gltf: ", path, " -> ", error_string(err))
	get_tree().quit(0 if err == OK else 1)

func _build_lighting() -> void:
	sky_mat = ShaderMaterial.new()
	sky_mat.shader = load("res://shaders/sky.gdshader")
	sky_mat.set_shader_parameter("panorama", load("res://assets/sky.hdr"))
	sky_mat.set_shader_parameter("saturation", Look.v("environment.sky_saturation"))
	sky_mat.set_shader_parameter("energy", Look.v("environment.sky_energy"))
	var sky := Sky.new()
	sky.sky_material = sky_mat
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	# AgX, the same view transform as Blender/Cycles: rolls off bright saturated colours the way film does.
	# The user preferred the Cycles look; ACES plus a contrast boost made colours loud and edges hard.
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	# every value below: art/look.json "environment" (with reasons)
	env.tonemap_white = Look.v("environment.tonemap_white")
	env.tonemap_exposure = Look.v("environment.tonemap_exposure")
	env.adjustment_enabled = true
	env.adjustment_contrast = Look.v("environment.adjustment_contrast")
	env.adjustment_saturation = Look.v("environment.adjustment_saturation")
	env.ssil_enabled = true
	env.ssil_radius = Look.v("environment.ssil_radius")
	env.ssil_intensity = Look.v("environment.ssil_intensity")
	env.sdfgi_use_occlusion = true
	env.sdfgi_enabled = true
	env.sdfgi_energy = Look.v("environment.sdfgi_energy")
	env.ssao_enabled = true
	env.ssao_radius = Look.v("environment.ssao_radius")
	env.ssao_intensity = Look.v("environment.ssao_intensity")
	env.ssao_power = Look.v("environment.ssao_power")
	env.ambient_light_sky_contribution = Look.v("environment.ambient_sky_contribution")
	env.ambient_light_color = Look.col("environment.ambient_color")
	var we := WorldEnvironment.new()
	we.environment = env
	environment = env
	add_child(we)

	sun = DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.light_angular_distance = Look.v("sun.angular_distance")
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL  # range set per zoom in _apply_camera
	sun.shadow_bias = Look.v("sun.shadow_bias")
	sun.shadow_normal_bias = Look.v("sun.shadow_normal_bias")
	sun.sky_mode = DirectionalLight3D.SKY_MODE_LIGHT_AND_SKY
	add_child(sun)

	moon = DirectionalLight3D.new()
	moon.shadow_enabled = true
	moon.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	moon.rotation_degrees = Vector3(-55.0, Look.v("sun.yaw_at_noon") + 120.0, 0)
	moon.sky_mode = DirectionalLight3D.SKY_MODE_LIGHT_ONLY
	add_child(moon)

# Sun path: rises in the east at 6:00, highest at 12:00, sets at 18:00; dims and warms near the horizon.
func _apply_time() -> void:
	var elevation: float = sin((hour - 6.0) / 12.0 * PI) * Look.v("sun.peak_elevation")
	sun.rotation_degrees = Vector3(-elevation, Look.v("sun.yaw_at_noon") + (hour - 12.0) * Look.v("sun.yaw_per_hour"), 0)
	# separate ramps (review 5): colour warms below sun.warm_below_elevation, but energy keeps a floor until the sun
	# actually sets, so a low sun is golden hour, not night; sky fill, moon and torches follow sunset (elevation < 0)
	var warm := smoothstep(0.0, Look.v("sun.warm_below_elevation"), elevation)  # flat near noon, fast near the horizon
	var day := clampf((elevation + 2.0) / 6.0, 0.0, 1.0)
	sun.light_energy = Look.v("sun.energy_day") * lerpf(Look.v("sun.energy_low"), 1.0, warm) * day
	sun.light_color = Look.col("sun.color_low").lerp(Look.col("sun.color_day"), warm)
	sun.visible = elevation > -2.0
	# The HDRI is a daytime sky: dim it (and the ambient light it gives) toward night.
	# shade stays neutral-to-cool at a low sun: the blue sky, not the orange-lit ground, is what lights it (round 6)
	sky_mat.set_shader_parameter("saturation", lerpf(Look.v("environment.sky_saturation_low"), Look.v("environment.sky_saturation"), warm))
	environment.background_energy_multiplier = lerpf(Look.v("environment.night_sky_energy"), 1.0, day) \
			* lerpf(1.0, lerpf(Look.v("environment.sky_energy_low"), 1.0, warm), day)  # also the GI fill; dims with a low sun
	# the camera exposes up for a low sun (as a photographer would), so golden hour keeps its contrast without going dark
	environment.tonemap_exposure = Look.v("environment.tonemap_exposure") * lerpf(1.0, lerpf(Look.v("environment.exposure_low"), 1.0, warm), day)  # daylight only; night has its own checks
	# moonlight: a dim blue key from high in the sky once the sun is down, so night stays readable
	moon.visible = day < 0.05
	for t in get_tree().get_nodes_in_group("torches"):
		(t as Light3D).visible = day < float(Look.v("placement.torch").lit_below_day)
	moon.light_energy = Look.v("sun.moon_energy")
	moon.light_color = Look.col("sun.moon_color")
	environment.ambient_light_energy = lerpf(Look.v("environment.ambient_energy_night"), Look.v("environment.ambient_energy_day"), day)

func _apply_camera() -> void:
	var pose := CameraZoom.evaluate(zoom)
	var focus := player.position + Vector3(0, 0.2, 0)
	var basis := Basis.from_euler(Vector3(deg_to_rad(-pose.pitch), deg_to_rad(-pose.yaw), 0), EULER_ORDER_YXZ)
	camera.transform = Transform3D(basis, focus + basis.z * pose.distance)
	camera.fov = pose.fov
	# Shadow range must reach the far edge of what the camera sees. The far iso camera sits ~43 m from the ground it
	# frames, so a fixed 40 m range left the far view with no sun shadows at all (art review 2). One orthogonal
	# cascade fitted to the visible depth band: full resolution, no split seams.
	var height: float = pose.distance * sin(deg_to_rad(pose.pitch))
	var far_ground: float = height / sin(deg_to_rad(maxf(pose.pitch - pose.fov / 2.0, 5.0)))
	sun.directional_shadow_max_distance = far_ground + Look.v("sun.shadow_margin")
	moon.directional_shadow_max_distance = sun.directional_shadow_max_distance
	camera.near = maxf(0.05, (height - 12.0) / sin(deg_to_rad(pose.pitch + pose.fov / 2.0)))  # start the depth range near the scene

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			zoom_target = clampf(zoom_target + ZOOM_STEP, 0.0, 1.0)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			zoom_target = clampf(zoom_target - ZOOM_STEP, 0.0, 1.0)
	elif event is InputEventKey and event.pressed and event.keycode == KEY_T:
		hour = fmod(hour + 1.0, 24.0)
		_apply_time()

func _process(delta: float) -> void:
	zoom = lerpf(zoom, zoom_target, minf(1.0, delta * ZOOM_SPEED))
	var basis := Basis.from_euler(Vector3(0, deg_to_rad(-CameraZoom.YAW), 0))
	var dir := Vector3.ZERO
	if Input.is_key_pressed(KEY_W): dir -= basis.z
	if Input.is_key_pressed(KEY_S): dir += basis.z
	if Input.is_key_pressed(KEY_D): dir += basis.x
	if Input.is_key_pressed(KEY_A): dir -= basis.x
	if dir != Vector3.ZERO:
		player.position += dir.normalized() * MOVE_SPEED * delta
	_apply_camera()

func _capture(vp: SubViewport, dir: String) -> void:
	DirAccess.make_dir_recursive_absolute(dir)
	var home := player.position
	var river := Vector3(4.0, home.y, Village.river_center(4.0) + 3.0)
	for shot in [["godot_far", 0.0, 11.0, home], ["godot_mid", 1.0, 11.0, home], ["godot_far_dusk", 0.0, 16.8, home], ["godot_far_night", 0.0, 22.0, home], ["godot_far_morning", 0.0, 9.0, home],
			["godot_river", 0.0, 11.0, river]]:
		var only := _user_arg("--only=")
		if not only.is_empty() and only != shot[0]:
			continue
		zoom = shot[1]
		zoom_target = shot[1]
		hour = shot[2]
		player.position = shot[3]
		_apply_time()
		for i in SETTLE_FRAMES:
			await RenderingServer.frame_post_draw
		if not _save_shot(vp, dir.path_join(shot[0] + ".png")):
			return
		if _user_arg("--fill-pass=") == "1":
			# Same frame with the sun off: tools/art_check.py divides the two to find which pixels are in shadow
			# (no sun contribution) wherever the shadows fall, instead of fixed screen rectangles.
			sun.visible = false
			for i in SETTLE_FRAMES:
				await RenderingServer.frame_post_draw
			if not _save_shot(vp, dir.path_join(shot[0] + "_fill.png")):
				return
			_apply_time()
			# lighting-only pass (white albedo): the colour of the light itself, so shadow tint is measured without
			# material colour mixed in
			vp.debug_draw = Viewport.DEBUG_DRAW_LIGHTING
			# linear tonemap at reduced exposure: AgX capped lit light at ~0.89 and bleached its colour, which flattened
			# key:fill and warmth (2026-10-16)
			environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
			environment.tonemap_exposure = Look.v("checks.light_pass_exposure")
			for i in SETTLE_FRAMES:
				await RenderingServer.frame_post_draw
			if not _save_shot(vp, dir.path_join(shot[0] + "_light.png")):
				return
			vp.debug_draw = Viewport.DEBUG_DRAW_DISABLED
			# material-ID pass: each material emits its flat class colour (art/look.json checks.id_colors), linear
			# exposure 1, so tools/art_check.py can measure one material at a time
			environment.tonemap_exposure = 1.0
			RenderingServer.global_shader_parameter_set("id_pass", 1.0)
			var decals := find_children("*", "Decal", true, false)  # skirt decals would paint over the ground's ID
			for d in decals:
				d.visible = false
			for i in SETTLE_FRAMES:
				await RenderingServer.frame_post_draw
			if not _save_shot(vp, dir.path_join(shot[0] + "_id.png")):
				return
			RenderingServer.global_shader_parameter_set("id_pass", 0.0)
			for d in decals:
				d.visible = true
			environment.tonemap_mode = Environment.TONE_MAPPER_AGX
			_apply_time()
	get_tree().quit(0)

func _save_shot(vp: SubViewport, path: String) -> bool:
	var image := vp.get_texture().get_image()
	image.resize(SHOT_SIZE.x, SHOT_SIZE.y, Image.INTERPOLATE_CUBIC)  # softer than Lanczos, like Cycles' 1.5 px pixel filter
	var err := image.save_png(path)
	if err != OK:
		push_error("capture failed: %s (%s)" % [path, error_string(err)])
		get_tree().quit(1)
		return false
	print("captured ", path)
	return true
