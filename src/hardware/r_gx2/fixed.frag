#version 450 core
layout (location = 0) in vec2 tex_coord;
layout (location = 0) out vec4 colour;

void main() {
    // YUCK!
    colour = vec4(tex_coord, 0.0, 1.0);
}