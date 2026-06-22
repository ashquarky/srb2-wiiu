#version 450 core
layout (location = 0) in vec2 tex_coord;
layout (location = 0) out vec4 colour;

layout (binding = 0) uniform sampler2D tex;

void main() {
    // YUCK!
    colour = texture(tex, tex_coord);
}