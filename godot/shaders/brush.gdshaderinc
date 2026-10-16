// Painterly brush texture in world space (Disco Elysium study, art/look.json stylise.surface; 0 = off).
// Short stretched strokes, oriented per ~0.5 m cell, projected on the plane the surface faces most (triplanar pick),
// so strokes follow walls on walls and lie flat on the ground.
float brush_hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float brush_noise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(brush_hash(i), brush_hash(i + vec2(1, 0)), u.x), mix(brush_hash(i + vec2(0, 1)), brush_hash(i + vec2(1, 1)), u.x), u.y);
}
float brush_layer(vec2 p, float scale, float seed) {
	vec2 cell = floor(p * 2.0 + seed);
	float a = brush_hash(cell) * 3.14159;
	mat2 r = mat2(vec2(cos(a), sin(a)), vec2(-sin(a), cos(a)));
	vec2 q = r * p * scale;
	return brush_noise(vec2(q.x * 0.25, q.y * 2.5) + seed);  // strokes ~10x longer than wide
}
float brush_stroke(vec3 wp, vec3 n, float scale) {
	vec3 a = abs(n);
	vec2 p = a.y > max(a.x, a.z) ? wp.xz : (a.x > a.z ? wp.zy : wp.xy);
	return mix(brush_layer(p, scale, 0.0), brush_layer(p, scale, 17.0), 0.5);  // two overlapping layers
}
