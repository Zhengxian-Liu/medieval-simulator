// River surface: colour by water depth (shallow -> deep) over the visible bed, slow layered ripple normals so the
// sky and sun break up into moving highlights, and a darkened wet band where the water meets the bank.
// No shore foam: a constant-width foam ribbon read as cheap real-time (art review 2026-10-16).
shader_type spatial;
render_mode cull_disabled;

uniform sampler2D depth_tex : hint_depth_texture, filter_linear_mipmap;
uniform vec3 shallow : source_color = vec3(0.22, 0.34, 0.30);
uniform vec3 deep : source_color = vec3(0.03, 0.13, 0.16);
uniform float depth_scale = 1.4;  // metres to reach the deep colour
uniform float roughness = 0.08;
uniform float specular = 0.3;
uniform float ripple = 1.0;  // ripple normal strength
uniform vec2 flow = vec2(0.25, 0.02);  // m/s, roughly along the river (+x)

varying vec3 world_pos;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float height(vec2 p) {
	return vnoise(p * 1.3 - flow * TIME * 1.3) * 0.6 + vnoise(p * 3.7 + vec2(flow.y, flow.x) * TIME * 2.0) * 0.4;  // no 9x octave: it sparkled as white noise (review 5)
}

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	float d = texture(depth_tex, SCREEN_UV).r;
	vec4 behind = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, d, 1.0);
	behind.xyz /= behind.w;
	float depth = max(VERTEX.z - behind.z, 0.0);  // view space, metres of water along the view ray
	float k = clamp(depth / depth_scale, 0.0, 1.0);
	// ripple normal from the height field (finite differences), in world space -> view space
	vec2 p = world_pos.xz;
	float e = 0.05;
	float h0 = height(p);
	vec3 n = normalize(vec3((h0 - height(p + vec2(e, 0.0))) * ripple, e * 4.0, (h0 - height(p + vec2(0.0, e))) * ripple));
	NORMAL = normalize((VIEW_MATRIX * vec4(n, 0.0)).xyz);
	ALBEDO = mix(shallow, deep, k) * mix(0.55, 1.0, smoothstep(0.0, 0.15, depth));  // dark wet edge at the waterline
	ALPHA = mix(0.8, 0.97, k);
	ROUGHNESS = roughness;
	SPECULAR = specular;
}
