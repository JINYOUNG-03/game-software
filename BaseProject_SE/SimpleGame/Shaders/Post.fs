#version 330
in vec2 uv;
out vec4 FragColor;
uniform sampler2D u_Scene;
uniform sampler2D u_Glow;
uniform vec2 u_Pixel;
uniform int u_Mode;
uniform float u_Time;
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
    vec3 color = texture(u_Scene, uv).rgb + texture(u_Glow, uv).rgb * .28;
    float luminance = dot(color, vec3(.2126, .7152, .0722));
    // Cool overcast dawn in the shadows, gentle amber in paper windows and lamps.
    vec3 cool = vec3(.94, 1.01, 1.055);
    vec3 warm = vec3(1.035, 1.015, .955);
    color *= mix(cool, warm, smoothstep(.26, .74, luminance));
    color = (color - .5) * 1.035 + .5 + vec3(.012, .017, .014);
    float veil = smoothstep(.35, 1.0, uv.y) *
                 (.018 + .009 * sin(uv.x * 7.0 + uv.y * 4.0 + u_Time * .035));
    color = mix(color, vec3(.52, .63, .61), veil);
    float vignette = 1.0 - .10 * smoothstep(.3, .78, length((uv - .5) * vec2(1.0, .85)));
    FragColor = vec4(clamp(color * vignette, 0.0, 1.0), 1);
}
