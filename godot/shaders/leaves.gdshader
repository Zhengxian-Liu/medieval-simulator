// Leaf cards (tools/blender/foliage.py): atlas colour with a separate opacity map cut out per pixel,
// alpha-to-coverage so the cut edges stay smooth under MSAA, and back-lighting so sunlit crowns glow at the rim.
shader_type spatial;
render_mode cull_disabled;

uniform sampler2D color_tex : source_color, filter_linear_mipmap_anisotropic;
uniform sampler2D opacity_tex : hint_default_white, filter_linear_mipmap_anisotropic;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap_anisotropic;
uniform vec3 tint : source_color = vec3(0.33, 0.42, 0.26);  // atlas leaves are spring-bright; oak leaf reflectance is ~10%
uniform float variation = 0.35;
uniform float backlight = 0.3;

float hash(vec3 p) { return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }

void fragment() {
	vec3 c = texture(color_tex, UV).rgb * tint;
	c *= 1.0 - variation * hash(floor(NODE_POSITION_WORLD * 3.0));  // each plant a little different
	ALBEDO = c * mix(0.35, 1.0, COLOR.r);  // vertex colour = canopy depth (tools/blender/foliage.py): dark interior
	AO = COLOR.r;
	ALPHA = texture(opacity_tex, UV).r;
	ALPHA_SCISSOR_THRESHOLD = 0.5;
	ALPHA_ANTIALIASING_EDGE = 0.3;
	NORMAL_MAP = texture(normal_tex, UV).rgb;
	ROUGHNESS = 0.75;
	BACKLIGHT = c * backlight;
}
