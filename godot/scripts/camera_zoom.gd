# Camera pose for one zoom level. t=0: near-orthographic isometric overview; t=1: ~10 m mid-range view.
# Distance is derived from FOV so the framed area shrinks smoothly while the FOV opens (no pop).
# Framing values live in art/look.json (camera.*).
extends RefCounted

const Look = preload("res://scripts/look.gd")
const ASPECT := 16.0 / 9.0
static var FAR_FOV: float = Look.v("camera.far_fov")
static var NEAR_FOV: float = Look.v("camera.near_fov")
static var FAR_WIDTH: float = Look.v("camera.far_width")
static var NEAR_WIDTH: float = Look.v("camera.near_width")
static var FAR_PITCH: float = Look.v("camera.far_pitch")
static var NEAR_PITCH: float = Look.v("camera.near_pitch")
static var YAW: float = Look.v("camera.yaw")

static func framed_width(pose: Dictionary) -> float:
	return 2.0 * pose.distance * tan(deg_to_rad(pose.fov) / 2.0) * ASPECT

static func evaluate(t: float) -> Dictionary:
	t = clampf(t, 0.0, 1.0)
	var ease_t := t * t * (3.0 - 2.0 * t)
	var fov := lerpf(FAR_FOV, NEAR_FOV, ease_t)
	# Geometric interpolation: each wheel notch zooms by the same ratio.
	var width := FAR_WIDTH * pow(NEAR_WIDTH / FAR_WIDTH, t)
	return {
		distance = width / (2.0 * tan(deg_to_rad(fov) / 2.0) * ASPECT),
		fov = fov,
		pitch = lerpf(FAR_PITCH, NEAR_PITCH, ease_t),
		yaw = YAW,
	}
