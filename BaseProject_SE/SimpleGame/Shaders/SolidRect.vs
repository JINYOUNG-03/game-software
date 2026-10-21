#version 330
layout(location = 0) in vec4 a_CornersA;
layout(location = 1) in vec4 a_CornersB;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in vec4 a_UV;
layout(location = 4) in float a_Material;
layout(location = 5) in vec4 a_Surface;
uniform vec2 u_Screen;
uniform vec4 u_WorldView;
uniform bool u_WorldShading;
uniform float u_Time;
out vec2 v_Local;
out vec2 v_TexCoord;
out vec2 v_World;
out vec2 v_Screen;
flat out vec4 v_Color;
flat out vec4 v_Surface;
flat out int v_Material;

vec2 gridPosition(vec2 screen)
{
    vec2 delta = screen - u_WorldView.zw;
    return u_WorldView.xy + vec2(delta.x / 96.0 + delta.y / 48.0,
                                 delta.y / 48.0 - delta.x / 96.0);
}

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
    v_World = gridPosition(p);
    v_Color = a_Color;
    v_Surface = a_Surface;
    v_Material = int(a_Material);
    if (u_WorldShading && (v_Material == 13 || v_Material == 14 || v_Material == 26 || v_Material == 27))
    {
        vec2 center = gridPosition((lo + hi) * .5);
        float phase = dot(center, vec2(.71, .43));
        p.x += sin(u_Time * 1.25 + phase) * (v_Material == 13 || v_Material == 26 ? 1.2 : 2.0) * v_Local.y;
    }
    v_Screen = p;
    gl_Position = vec4(p.x / u_Screen.x * 2.0 - 1.0, 1.0 - p.y / u_Screen.y * 2.0, 0, 1);
}
