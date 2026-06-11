#version 450 core
layout (location = 0) in vec2 aPosition;
layout (location = 1) in vec2 aTexCoord;
layout (location = 0) out vec2 tex_coord;

void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    tex_coord = aTexCoord;
}