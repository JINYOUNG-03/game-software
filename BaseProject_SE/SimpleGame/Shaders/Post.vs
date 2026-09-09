#version 330
in vec3 a_Position;
out vec2 uv;
void main() { uv=a_Position.xy+0.5; gl_Position=vec4(a_Position.xy*2.0,0,1); }
