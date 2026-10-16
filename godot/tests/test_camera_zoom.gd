# Headless: godot --headless --path godot -s res://tests/test_camera_zoom.gd  (exit 1 on failure)
# Failure modes:
# 1. Out-of-range zoom produces a pose outside the far..near envelope.
# 2. Far end loses the near-orthographic isometric look (wide FOV, wrong pitch, wrong framing).
# 3. Near end is not a ~10 m mid-range view (narrow FOV, camera far away).
# 4. Zooming in moves the camera away or widens the framed area at some step (visible pop).
extends SceneTree

const CameraZoom = preload("res://scripts/camera_zoom.gd")

var failures := 0

func check(ok: bool, what: String) -> void:
	if not ok:
		failures += 1
		printerr("FAIL: ", what)

func _init() -> void:
	check(CameraZoom.evaluate(-3.0) == CameraZoom.evaluate(0.0), "clamps below 0")
	check(CameraZoom.evaluate(7.0) == CameraZoom.evaluate(1.0), "clamps above 1")

	var far := CameraZoom.evaluate(0.0)
	check(far.fov <= 15.0, "far FOV is near-orthographic")
	check(far.pitch == CameraZoom.FAR_PITCH, "far pitch is the iso pitch")
	check(far.yaw == CameraZoom.YAW, "far yaw is the iso yaw")
	check(is_equal_approx(CameraZoom.framed_width(far), CameraZoom.FAR_WIDTH), "far framing width")

	var near := CameraZoom.evaluate(1.0)
	check(near.fov >= 30.0, "near FOV is perspective")
	check(near.distance <= 20.0, "near camera is close")
	check(is_equal_approx(CameraZoom.framed_width(near), CameraZoom.NEAR_WIDTH), "near framing ~10 m")

	var prev := far
	for step in range(1, 201):
		var cur := CameraZoom.evaluate(step / 200.0)
		check(cur.distance <= prev.distance, "distance shrinks at step %d" % step)
		check(CameraZoom.framed_width(cur) <= CameraZoom.framed_width(prev) + 1e-9, "framing shrinks at step %d" % step)
		prev = cur

	print("camera_zoom: %s" % ("PASS" if failures == 0 else "%d FAILED" % failures))
	quit(1 if failures else 0)
