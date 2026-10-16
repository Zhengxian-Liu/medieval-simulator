// Shared material for scripted hero assets (tools/hero_cottage.py): a Poly Haven PBR set sampled through the
// asset's own UVs, plus weathering every asset gets the same way so they read as one world:
// splash dirt rising from the asset's base, and moss on upward-facing surfaces, both broken up by noise.
shader_type spatial;
render_mode cull_disabled;

uniform sampler2D albedo_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D rough_tex : hint_roughness_r, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec3 tint : source_color = vec3(1.0);
uniform float dirt_height = 1.0;  // metres above the asset origin that splash dirt reaches
uniform float moss = 0.0;         // 0..1 coverage on upward faces
uniform vec3 dirt_color : source_color = vec3(0.2, 0.15, 0.1);
uniform vec3 moss_color : source_color = vec3(0.1, 0.13, 0.05);

varying vec3 world_pos;
varying vec3 world_normal;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	world_normal = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

void fragment() {
	vec3 c = texture(albedo_tex, UV).rgb * tint;
	float n = vnoise(world_pos.xz * 1.3 + world_pos.y * 0.7) * 0.6 + vnoise(world_pos.xz * 4.1 + world_pos.y * 3.0) * 0.4;
	float height = world_pos.y - NODE_POSITION_WORLD.y;
	float dirt = clamp(1.0 - height / dirt_height, 0.0, 1.0) * smoothstep(0.25, 0.7, n);
	c = mix(c, dirt_color, dirt * 0.75);
	float up = smoothstep(0.45, 0.9, world_normal.y);
	c = mix(c, moss_color, moss * up * smoothstep(0.5, 0.78, n));
	ALBEDO = c;
	ROUGHNESS = texture(rough_tex, UV).r;
	NORMAL_MAP = texture(normal_tex, UV).rgb;
}
