// Village ground: grass and trampled dirt blended by a painted mask (paths, yards, the square),
// with noise breaking up the mask edge and large-scale colour variation so the grass never reads as one tile.
shader_type spatial;

uniform sampler2D grass_albedo : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D grass_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D grass_rough : hint_roughness_r, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D dirt_albedo : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D dirt_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D dirt_rough : hint_roughness_r, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D mask : filter_linear;  // r = how trampled (0 grass .. 1 bare dirt)
uniform vec4 mask_rect;                  // world x0, z0, width, depth covered by the mask
uniform vec3 grass_tint : source_color = vec3(0.5, 0.66, 0.42);
uniform vec3 soil_tint = vec3(1.0);  // warm brown on the dirt/gravel scan (art/look.json materials.ground)
uniform float grass_tiles = 0.25;        // tiles per metre
uniform float dirt_tiles = 0.4;
uniform float ground_value = 0.62;  // darkens the bright photo-scanned gravel/grass: ground should sit at ~0.25-0.35 value
uniform vec4 river;  // centre z0, amplitude, frequency, phase (same curve as village.gd river_center)
uniform float river_half = 3.5;

varying vec3 world_pos;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec2 uvg = world_pos.xz * grass_tiles;
	vec2 uvd = world_pos.xz * dirt_tiles;
	float m = texture(mask, (world_pos.xz - mask_rect.xy) / mask_rect.zw).r;
	float n = vnoise(world_pos.xz * 0.9) * 0.6 + vnoise(world_pos.xz * 3.7) * 0.4;
	float dirt = smoothstep(0.38, 0.62, m + (n - 0.5) * 0.55);
	float patchy = mix(0.78, 1.12, vnoise(world_pos.xz * 0.12));  // big soft colour patches in the grass
	vec3 grass = texture(grass_albedo, uvg).rgb * grass_tint * patchy;
	vec3 soil = texture(dirt_albedo, uvd).rgb * soil_tint;
	// worn fringe beside paths and yards: yellowed, flattened grass before the bare dirt starts
	float fringe = smoothstep(0.12, 0.3, m) * (1.0 - dirt);
	grass = mix(grass, grass * vec3(1.15, 1.05, 0.7), fringe * 0.7);
	vec3 col = mix(grass, soil, dirt);
	// large damp/dry patches (several metres): the ground gets big tonal shapes instead of uniform speckle
	col *= mix(0.72, 1.0, smoothstep(0.25, 0.75, vnoise(world_pos.xz * 0.05 + 7.0)));
	// banks: pale sand from just outside the bank into the shallows, dark wet mud under deeper water
	float rd = abs(world_pos.z - (river.x + river.y * sin(world_pos.x * river.z + river.w)));
	float sand = 1.0 - smoothstep(river_half + 0.4, river_half + 1.8, rd + (n - 0.5) * 0.9);
	col = mix(col, soil * vec3(1.55, 1.38, 1.05), sand);
	float wet = 1.0 - smoothstep(river_half - 0.6, river_half + 0.1, rd);
	col = mix(col, soil * 0.55, wet);
	ALBEDO = col * ground_value;
	ROUGHNESS = mix(texture(grass_rough, uvg).r, texture(dirt_rough, uvd).r, dirt);
	NORMAL_MAP = mix(texture(grass_normal, uvg).rgb, texture(dirt_normal, uvd).rgb, dirt);
}
