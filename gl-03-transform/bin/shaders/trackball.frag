#ifdef GL_ES
	#ifndef GL_FRAGMENT_PRECISION_HIGH	// highp may not be defined
		#define highp mediump
	#endif
	precision highp float; // default precision needs to be defined
#endif

// inputs from vertex shader
in vec3 norm;
in vec2 tc;	// used for texture coordinate visualization
uniform sampler2D albedo_texture;
uniform bool has_texture;
uniform vec4 base_color;
uniform vec4 palette_colors[4];
uniform bool outline_pass;
uniform bool toon_enabled;

layout(location=0) out vec4 fragColor;
layout(location=1) out vec4 gbuffer_normal;

void main()
{
	gbuffer_normal = vec4(normalize(norm) * 0.5 + 0.5, 1.0);
	if (outline_pass)
	{
		fragColor = vec4(0.015, 0.012, 0.01, 1.0);
		return;
	}
	vec4 material_color = has_texture ? texture(albedo_texture, tc) : base_color;
	if (!toon_enabled)
	{
		fragColor = material_color;
		return;
	}
	vec3 light_direction = normalize(vec3(-0.4, 0.7, 0.8));
	float diffuse = max(dot(normalize(norm), light_direction), 0.0);
	float toon_light = diffuse < 0.3 ? 0.38 : diffuse < 0.68 ? 0.78 : 1.15;
	vec3 contrasted_color = clamp((material_color.rgb - 0.5) * 1.35 + 0.5, 0.0, 1.0);
	vec3 shaded_color = clamp(contrasted_color * toon_light, 0.0, 1.0);
	int nearest = 0;
	float nearest_distance = distance(shaded_color, palette_colors[0].rgb);
	for (int i = 1; i < 4; ++i)
	{
		float candidate_distance = distance(shaded_color, palette_colors[i].rgb);
		if (candidate_distance < nearest_distance)
		{
			nearest = i;
			nearest_distance = candidate_distance;
		}
	}
	vec3 cartoon_color = palette_colors[nearest].rgb;
	fragColor = vec4(cartoon_color, material_color.a);
}
