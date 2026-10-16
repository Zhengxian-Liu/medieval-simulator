// Sky from the HDRI, partially desaturated: under SDFGI the sky is the shadow fill light, and the full-blue HDRI gave
// every shadow a blue cast. art/look.json environment.sky_saturation / sky_energy.
shader_type sky;

uniform sampler2D panorama : filter_linear;  // HDR, already linear (no source_color conversion)
uniform float saturation = 0.5;
uniform float energy = 1.0;

void sky() {
	vec2 uv = vec2(atan(EYEDIR.x, -EYEDIR.z) / (2.0 * PI) + 0.5, acos(clamp(EYEDIR.y, -1.0, 1.0)) / PI);
	vec3 c = texture(panorama, uv).rgb;
	float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
	COLOR = mix(vec3(l), c, saturation) * energy;
}
