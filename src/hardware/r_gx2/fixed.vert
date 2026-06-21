#version 450 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aTexCoord;
layout (location = 0) out vec2 tex_coord;

layout (binding = 0) uniform UBO {
    mat4 uModelView;
    mat4 uProjection;
};

void main() {
    gl_Position = uProjection * uModelView * vec4(aPosition, 1.0);
    tex_coord = aTexCoord;
}