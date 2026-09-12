#version 330

in vec2 uv;
out vec4 fragColor;

uniform sampler2D scene_texture;
uniform sampler2D normal_texture;
uniform sampler2D depth_texture;
uniform vec2 texel_size;
uniform vec3 line_color;
uniform bool linework_enabled;
uniform int line_radius;

vec3 decode_normal(vec2 coordinate)
{
    return normalize(texture(normal_texture, coordinate).xyz * 2.0 - 1.0);
}

float depth_edge(vec2 coordinate, float center_depth)
{
    return abs(texture(depth_texture, coordinate).r - center_depth);
}

void main()
{
    vec3 scene_color = texture(scene_texture, uv).rgb;
    if (!linework_enabled)
    {
        fragColor = vec4(scene_color, 1.0);
        return;
    }

    float center_depth = texture(depth_texture, uv).r;
    if (center_depth >= 0.9999)
    {
        fragColor = vec4(scene_color, 1.0);
        return;
    }

    vec3 center_normal = decode_normal(uv);
    float normal_response = 0.0;
    float depth_response = 0.0;
    for (int y = -4; y <= 4; ++y)
    {
        for (int x = -4; x <= 4; ++x)
        {
            if (x == 0 && y == 0 || abs(x) > line_radius || abs(y) > line_radius) continue;
            vec2 offset = vec2(float(x), float(y)) * texel_size;
            vec3 neighbor_normal = decode_normal(uv + offset);
            normal_response = max(normal_response, 1.0 - dot(center_normal, neighbor_normal));
            depth_response = max(depth_response, depth_edge(uv + offset, center_depth));
        }
    }

    float normal_edge = smoothstep(0.16, 0.42, normal_response);
    float depth_edge_value = smoothstep(0.004, 0.02, depth_response);
    float edge = max(normal_edge, depth_edge_value);
    fragColor = vec4(mix(scene_color, line_color, edge), 1.0);
}