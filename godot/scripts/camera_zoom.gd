# Camera pose for one zoom level. t=0: near-orthographic isometric overview; t=1: ~10 m mid-range view.
# Distance is derived from FOV so the framed area shrinks smoothly while the FOV opens (no pop).
extends RefCounted

const FAR_FOV := 12.0     # vertical, degrees
const NEAR_FOV := 35.0
const FAR_WIDTH := 22.0   # metres of ground across the screen (Pine & Parcel frames ~20-25 m)
const NEAR_WIDTH := 10.0
const FAR_PITCH := 45.0   # degrees below the horizon
const NEAR_PITCH := 32.0
const YAW := 45.0
const ASPECT := 16.0 / 9.0

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
