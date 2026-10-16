# Entry scene: builds the village, lighting with a time of day, and the iso-to-mid zoom camera.
# Mouse wheel zooms, WASD moves (screen-relative), T advances the clock by one hour.
# `godot --path godot -- --capture=<abs dir>` renders far/mid/dusk shots at 1920x1080 into <dir> and quits.
extends Node3D

const CameraZoom = preload("res://scripts/camera_zoom.gd")
const Village = preload("res://scripts/village.gd")

const ZOOM_STEP := 0.08
const ZOOM_SPEED := 6.0
const MOVE_SPEED := 4.0
const SHOT_SIZE := Vector2i(1920, 1080)
const SETTLE_FRAMES := 90  # SDFGI and SSAO converge over several frames

var zoom := 0.0
var zoom_target := 0.0
var hour := 10.0

var player: Node3D
var sun: DirectionalLight3D
var environment: Environment
var camera: Camera3D

func _ready() -> void:
	player = Village.new().build(self)
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
		vp.size = SHOT_SIZE
		vp.msaa_3d = Viewport.MSAA_4X
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
	camera = Camera3D.new()
	camera.name = "FarIsoCamera"
	add_child(camera)
	zoom = 0.0
	_apply_camera()
	_apply_time()
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var err := doc.append_from_scene(self, state)
	if err == OK:
		err = doc.write_to_filesystem(state, path)
	print("export_gltf: ", path, " -> ", error_string(err))
	get_tree().quit(0 if err == OK else 1)

func _build_lighting() -> void:
	var sky_mat := PanoramaSkyMaterial.new()
	sky_mat.panorama = load("res://assets/sky.hdr")
	var sky := Sky.new()
	sky.sky_material = sky_mat
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.tonemap_white = 6.0
	env.tonemap_exposure = 0.85
	env.adjustment_enabled = true
	env.adjustment_contrast = 1.15
	env.adjustment_saturation = 0.9
	env.sdfgi_enabled = true
	env.ssao_enabled = true
	env.ssao_radius = 1.2
	env.ssao_intensity = 2.0
	env.glow_enabled = true
	env.glow_intensity = 0.3
	var we := WorldEnvironment.new()
	we.environment = env
	environment = env
	add_child(we)

	sun = DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 120.0
	sun.sky_mode = DirectionalLight3D.SKY_MODE_LIGHT_AND_SKY
	add_child(sun)

# Sun path: rises in the east at 6:00, highest at 12:00, sets at 18:00; dims and warms near the horizon.
func _apply_time() -> void:
	var elevation := sin((hour - 6.0) / 12.0 * PI) * 55.0
	sun.rotation_degrees = Vector3(-elevation, 75.0 + (hour - 12.0) * 10.0, 0)  # from the upper left of the screen (classic iso key light), so the roofs and walls we see are lit
	var day := clampf(elevation / 20.0, 0.0, 1.0)
	sun.light_energy = lerpf(0.0, 1.4, day)
	sun.light_color = Color(1.0, 0.55, 0.3).lerp(Color(1.0, 0.96, 0.9), day)
	sun.visible = elevation > -2.0
	# The HDRI is a daytime sky: dim it (and the ambient light it gives) toward night.
	environment.background_energy_multiplier = lerpf(0.04, 1.0, day)
	environment.ambient_light_energy = lerpf(0.08, 0.6, day)  # the HDRI is bright; full strength washed out the sun shadows

func _apply_camera() -> void:
	var pose := CameraZoom.evaluate(zoom)
	var focus := player.position + Vector3(0, 0.2, 0)
	var basis := Basis.from_euler(Vector3(deg_to_rad(-pose.pitch), deg_to_rad(-pose.yaw), 0), EULER_ORDER_YXZ)
	camera.transform = Transform3D(basis, focus + basis.z * pose.distance)
	camera.fov = pose.fov

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
	for shot in [["godot_far", 0.0, 10.0], ["godot_mid", 1.0, 10.0], ["godot_far_dusk", 0.0, 17.6]]:
		zoom = shot[1]
		zoom_target = shot[1]
		hour = shot[2]
		_apply_time()
		for i in SETTLE_FRAMES:
			await RenderingServer.frame_post_draw
		var path: String = dir.path_join(shot[0] + ".png")
		var err := vp.get_texture().get_image().save_png(path)
		if err != OK:
			push_error("capture failed: %s (%s)" % [path, error_string(err)])
			get_tree().quit(1)
			return
		print("captured ", path)
	get_tree().quit(0)
