// Focal detail falloff (Disco Elysium study, art/look.json stylise.focal; strength 0 = node hidden): the frame keeps
// full detail around the centre and softens + desaturates slightly toward the edges, as a painter would.
shader_type spatial;
render_mode unshaded, depth_test_disabled, cull_disabled;
uniform sampler2D screen : hint_screen_texture, filter_linear_mipmap;
uniform float strength = 0.0;  // max blur in mip levels at the corners
uniform float radius = 0.55;   // fully sharp inside this distance from the centre (0..~0.7 = corner)
uniform float desaturate = 0.0;

void vertex() {
	POSITION = vec4(VERTEX.xy * 2.0, 1.0, 1.0);  // full-screen quad (QuadMesh 1x1)
}

void fragment() {
	float d = smoothstep(radius, 0.75, length((SCREEN_UV - 0.5) * vec2(1.6, 1.0)));
	vec3 c = textureLod(screen, SCREEN_UV, d * strength).rgb;
	float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
	ALBEDO = mix(c, vec3(l), d * desaturate);
}
