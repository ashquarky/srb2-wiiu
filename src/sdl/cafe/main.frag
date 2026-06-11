#version 450 core
layout (location = 0) in vec2 tex_coord;
layout (location = 0) out vec4 colour;

layout (binding = 0) uniform sampler2D tex;
layout (binding = 1) uniform sampler2D colour_table;

void main() {
    float index = texture(tex, tex_coord).x;
    colour = texture(colour_table, vec2(index, 0.0f));
}