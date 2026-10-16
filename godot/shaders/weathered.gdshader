// Shared material for scripted hero assets (tools/blender/buildings.py): a Poly Haven PBR set sampled through the
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
global uniform float id_pass;
uniform vec3 id_color = vec3(0.0);  // art/look.json checks.id_colors[slot]
uniform float ao_light_affect = 0.25;  // share of the baked AO that also darkens direct light
uniform float ao_strength = 1.0;  // vertex colour R = Cycles-baked AO, G = convexity (tools/blender/bake.py)
uniform float splash = 0.4;      // strength of ground splash dirt
uniform float grime = 0.15;      // grime in baked-AO crevices
uniform float edge_wear = 0.6;   // lightening on baked convex edges
uniform float roughness_min = 0.0;  // floor on the scanned roughness (straw, daub and rough stone are matte)
uniform float specular = 0.5;       // F0 control; dry organic materials reflect little
uniform float eave_age = 0.0;   // thatch: darkening toward the eave (vertex colour B = ridge..eave), wetter older straw
uniform float course_height = 0.0;  // thatch: vertical spacing of the laid courses (m); 0 = off
uniform float course_shadow = 0.0;  // darkening just below each course's butt ends
uniform float course_var = 0.0;     // per-course tone variation (each course laid from a different batch of straw)
uniform float bundle_width = 0.3;  // thatch: bundles laid side by side along each course (m)
uniform float bundle_var = 0.0;    // per-bundle tone variation
uniform float bundle_gap = 0.0;    // darkening in the gaps between bundles
uniform float ragged = 0.0;
uniform float bundle_relief = 0.0;  // normal tilt across each bundle and at each course step: lit like geometry        // per-bundle offset of the course edge (in courses): jagged butt ends
uniform float detile = 0.0;  // blend in a second, rescaled sample: hides the texture repeat (thatch)

varying vec3 world_pos;
varying vec3 world_normal;
varying float baked_ao;
varying float edge;
varying float slope_pos;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	world_normal = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
	baked_ao = COLOR.r;
	edge = COLOR.g;
	slope_pos = COLOR.b;  // thatch ridge (0) -> eave (1), baked by tools/blender/bake.py; 0 on other slots
}

void fragment() {
	vec3 c = texture(albedo_tex, UV).rgb;
	if (detile > 0.0) {
		vec2 uv2 = vec2(-UV.x * 0.71 + 0.37, UV.y * 0.83 + 0.19);  // same strand direction, different scale and phase
		c = mix(c, texture(albedo_tex, uv2).rgb, detile);
	}
	c *= tint * mix(0.88, 1.08, vnoise(world_pos.xz * 0.35 + world_pos.y * 0.2));  // large soft patches break the tile period
	float n = vnoise(world_pos.xz * 1.3 + world_pos.y * 0.7) * 0.6 + vnoise(world_pos.xz * 4.1 + world_pos.y * 3.0) * 0.4;
	float height = world_pos.y - NODE_POSITION_WORLD.y;
	float dirt = clamp(1.0 - height / dirt_height, 0.0, 1.0) * smoothstep(0.25, 0.7, n);
	c = mix(c, dirt_color, dirt * splash);
	float up = smoothstep(0.45, 0.9, world_normal.y);
	c = mix(c, moss_color, moss * up * smoothstep(0.5, 0.78, n));
	// ridge-to-eave ageing: lower courses darker and mossier, broken into bundle-size patches
	// plus bundle-scale (~0.5-1 m) tonal patches: real thatch is laid in bundles of differing age and density
	float patches = vnoise(world_pos.xz * 1.1 + world_pos.y * 0.7) * 0.65 + vnoise(world_pos.xz * 2.7 - world_pos.y) * 0.35;
	patches = smoothstep(0.3, 0.7, patches);  // value noise clusters near 0.5: stretch to full range so patches read
	float age = eave_age * smoothstep(0.15, 1.0, slope_pos) * mix(0.5, 1.2, patches);
	c *= mix(1.0, mix(0.45, 1.35, patches), step(0.001, slope_pos) * eave_age);
	c = mix(c, c * 0.45, clamp(age, 0.0, 1.0));
	c = mix(c, moss_color, clamp(age * moss * 0.8, 0.0, 1.0) * smoothstep(0.45, 0.75, patches));
	vec3 relief_normal = NORMAL;
	bool use_relief = false;
	if (course_height > 0.0 && slope_pos > 0.001) {
		// horizontal courses: world-height bands, wobbling a few cm along the roof so they are not ruled lines
		float y = (world_pos.y - NODE_POSITION_WORLD.y) / course_height
				+ (vnoise(world_pos.xz * 0.9) - 0.5) * 0.35;
		// bundles: cells along the course (horizontal tangent of the roof), widths wobbling, each with its own tone,
		// dark gaps between them, and its own butt-end height so course edges are jagged (round 8: smooth fur)
		vec2 along_dir = normalize(vec2(world_normal.z, -world_normal.x) + vec2(1e-4));
		float a = dot(world_pos.xz, along_dir) / bundle_width + vnoise(world_pos.xz * 1.7) * 0.8;
		float k0 = floor(y);
		a += hash(vec2(k0, 3.1)) * 7.0;  // bundles of neighbouring courses don't line up
		float b = floor(a);
		y += (hash(vec2(b, k0)) - 0.5) * ragged;
		float k = floor(y);
		float seed = NODE_POSITION_WORLD.x + NODE_POSITION_WORLD.z;
		c *= 1.0 + (hash(vec2(k, seed)) - 0.5) * 2.0 * course_var;
		c *= 1.0 + (hash(vec2(b + seed, k)) - 0.5) * 2.0 * bundle_var;
		float fa = fract(a);
		c *= 1.0 - bundle_gap * (1.0 - smoothstep(0.0, 0.18, min(fa, 1.0 - fa)));
		c *= mix(1.0 - course_shadow, 1.0, smoothstep(0.0, 0.3, 1.0 - fract(y)));  // dark band just under the butt ends above
		if (bundle_relief > 0.0) {
			// rounded bundles (normal turns across each one) and stepped courses (butt ends face down-slope): the
			// sun lights one flank and shades the other, as real bundle depth does; geometry would cost ~30k tris a roof
			vec3 along3 = vec3(along_dir.x, 0.0, along_dir.y);
			vec3 down = normalize(vec3(0.0, -1.0, 0.0) - world_normal * dot(vec3(0.0, -1.0, 0.0), world_normal) + vec3(1e-4));
			vec3 nw = world_normal + along3 * (fa - 0.5) * 2.0 * bundle_relief
					+ down * smoothstep(0.65, 1.0, fract(y)) * bundle_relief * 1.5;
			relief_normal = normalize((VIEW_MATRIX * vec4(normalize(nw), 0.0)).xyz);
			use_relief = true;
		}
	}
	ALBEDO = c;
	AO = mix(1.0, baked_ao, ao_strength);  // occludes ambient/GI light only, so direct sun still lands on the surface
	AO_LIGHT_AFFECT = ao_light_affect;       // and a little on direct light: crevices stay dark at noon too
	// grime collects where the AO says the surface is sheltered: under eaves, between beams, at the base
	ALBEDO = mix(ALBEDO, dirt_color * 0.6, (1.0 - baked_ao) * grime * ao_strength);
	// worn edges: lighter, drier, slightly rougher where beams, planks and stones turn a corner (breaks up flat shapes)
	float wear = smoothstep(0.25, 0.8, edge) * smoothstep(0.3, 0.7, n);
	ALBEDO = mix(ALBEDO, ALBEDO * 1.45 + vec3(0.03), wear * edge_wear);
	ROUGHNESS = clamp(max(texture(rough_tex, UV).r * mix(0.85, 1.15, n), roughness_min) + wear * 0.1, 0.0, 1.0);
	SPECULAR = specular;
	if (use_relief) {
		NORMAL = relief_normal;
	}
	NORMAL_MAP = texture(normal_tex, UV).rgb;
	if (id_pass > 0.5) {  // material-ID capture pass (main.gd): flat colour per material class, no lighting
		ALBEDO = vec3(0.0);
		SPECULAR = 0.0;
		ROUGHNESS = 1.0;
		EMISSION = id_color;
	}
}
