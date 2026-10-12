#version 330
layout(location = 0) in vec4 a_CornersA;
layout(location = 1) in vec4 a_CornersB;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in vec4 a_UV;
layout(location = 4) in float a_Material;
uniform vec2 u_Screen;
out vec2 v_Local;
out vec2 v_TexCoord;
flat out vec4 v_Color;
flat out int v_Material;
void main()
{
    vec2 corners[4] = vec2[4](a_CornersA.xy, a_CornersA.zw, a_CornersB.xy, a_CornersB.zw);
    int indices[6] = int[6](0, 1, 2, 0, 2, 3);
    vec2 p = corners[indices[gl_VertexID]];
    vec2 lo = min(min(corners[0], corners[1]), min(corners[2], corners[3]));
    vec2 hi = max(max(corners[0], corners[1]), max(corners[2], corners[3]));
    vec2 local = (p - lo) / max(hi - lo, vec2(0.0001));
    v_Local = vec2(local.x, 1.0 - local.y);
    v_TexCoord = a_UV.xy + local * a_UV.zw;
    v_Color = a_Color;
    v_Material = int(a_Material);
    gl_Position = vec4(p.x / u_Screen.x * 2.0 - 1.0, 1.0 - p.y / u_Screen.y * 2.0, 0, 1);
}
