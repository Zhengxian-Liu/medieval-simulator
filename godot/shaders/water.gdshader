// River surface: colour by water depth (shallow -> deep) plus foam where the water is shallow at the shore.
shader_type spatial;
render_mode cull_disabled;

uniform sampler2D depth_tex : hint_depth_texture, filter_linear_mipmap;
uniform vec3 shallow : source_color = vec3(0.30, 0.52, 0.48);
uniform vec3 deep : source_color = vec3(0.03, 0.13, 0.16);
uniform float depth_scale = 1.4;  // metres to reach the deep colour
uniform float foam_width = 0.25;  // metres of water depth that still shows foam

void fragment() {
	float d = texture(depth_tex, SCREEN_UV).r;
	vec4 behind = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, d, 1.0);
	behind.xyz /= behind.w;
	float depth = max(VERTEX.z - behind.z, 0.0);  // view space, metres of water along the view ray
	float k = clamp(depth / depth_scale, 0.0, 1.0);

	vec3 world = (INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float ripple = 0.5 + 0.5 * sin(world.x * 2.3 + TIME * 1.6) * sin(world.z * 3.1 - TIME * 1.1);
	float foam = (1.0 - smoothstep(0.0, foam_width, depth)) * mix(0.6, 1.0, ripple);

	ALBEDO = mix(mix(shallow, deep, k), vec3(0.92), foam);
	ALPHA = mix(0.65, 0.97, k) + foam;
	ROUGHNESS = mix(0.04, 0.6, foam);
	SPECULAR = 0.6;
}
