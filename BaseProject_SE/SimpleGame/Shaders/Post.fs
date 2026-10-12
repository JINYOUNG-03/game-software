#version 330
in vec2 uv;
out vec4 FragColor;
uniform sampler2D u_Scene;
uniform sampler2D u_Glow;
uniform vec2 u_Pixel;
uniform int u_Mode;
void main()
{
    if (u_Mode == 0)
    {
        vec3 bright = vec3(0);
        for (int x = 0; x < 2; ++x)
        {
            for (int y = 0; y < 2; ++y)
            {
                vec3 sampleColor = texture(u_Scene, uv + (vec2(x, y) - .5) * u_Pixel * .5).rgb;
                bright += max(sampleColor - vec3(.66), vec3(0));
            }
        }
        FragColor = vec4(bright * .25, 1);
        return;
    }
    if (u_Mode == 1 || u_Mode == 2)
    {
        vec2 axis = u_Mode == 1 ? vec2(u_Pixel.x, 0) : vec2(0, u_Pixel.y);
        vec3 blur = texture(u_Scene, uv).rgb * .227027;
        blur += texture(u_Scene, uv + axis * 1.384615).rgb * .316216;
        blur += texture(u_Scene, uv - axis * 1.384615).rgb * .316216;
        blur += texture(u_Scene, uv + axis * 3.230769).rgb * .070270;
        blur += texture(u_Scene, uv - axis * 3.230769).rgb * .070270;
        FragColor = vec4(blur, 1);
        return;
    }
    vec3 color = texture(u_Scene, uv).rgb + texture(u_Glow, uv).rgb * .65;
    color = mix(vec3(dot(color, vec3(.299, .587, .114))), color, .88);
    color *= vec3(.94, 1.02, 1.04);
    float vignette = 1.0 - .30 * smoothstep(.2, .72, length(uv - .5));
    FragColor = vec4(color * vignette, 1);
}
