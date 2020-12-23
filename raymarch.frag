#version 450

layout(location = 0) in vec2 fragCoord;

layout(location = 0) out vec4 FragColor;

void main()
{
    // Time varying pixel color
    vec3 col = 0.5 + 0.5 * cos(fragCoord.xyx + vec3(0, 2, 4));

    FragColor = vec4(col, 1.0);
}
