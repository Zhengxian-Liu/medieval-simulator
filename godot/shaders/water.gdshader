// River surface: colour by water depth (shallow -> deep) over the visible bed. No shore foam: a constant-width
// foam ribbon read as cheap real-time (art review 2026-10-16); the shore reads through the wet-mud ground band.
shader_type spatial;
render_mode cull_disabled;

uniform sampler2D depth_tex : hint_depth_texture, filter_linear_mipmap;
uniform vec3 shallow : source_color = vec3(0.22, 0.34, 0.30);
uniform vec3 deep : source_color = vec3(0.03, 0.13, 0.16);
uniform float depth_scale = 1.4;  // metres to reach the deep colour

void fragment() {
	float d = texture(depth_tex, SCREEN_UV).r;
	vec4 behind = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, d, 1.0);
	behind.xyz /= behind.w;
	float depth = max(VERTEX.z - behind.z, 0.0);  // view space, metres of water along the view ray
	float k = clamp(depth / depth_scale, 0.0, 1.0);
	ALBEDO = mix(shallow, deep, k);
	ALPHA = mix(0.8, 0.97, k);
	ROUGHNESS = 0.12;
	SPECULAR = 0.6;
}
