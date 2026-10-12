#version 330

layout(location = 0) out vec4 FragColor;

flat in vec4 v_Color;
in vec2 v_TexCoord;
uniform sampler2D u_Texture;
in vec2 v_Local;
flat in int v_Material;
uniform float u_Time;

float noise(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main()
{
    if (v_Material == 10)
    {
        FragColor = vec4(v_Color.rgb, v_Color.a * texture(u_Texture, v_TexCoord).r);
        return;
    }
    if (v_Material == 11)
    {
        vec4 cached = texture(u_Texture, vec2(v_TexCoord.x, 1.0 - v_TexCoord.y));
        // The cached layer was blended over transparent black. Convert its filtered edges
        // back to straight alpha before the normal source-alpha composition.
        if (cached.a > 0.0001) { cached.rgb /= cached.a; }
        FragColor = cached * v_Color;
        return;
    }
    vec3 c = v_Color.rgb;
    float a = v_Color.a;
    vec2 p = v_Local;
    float n = 0.0;
    if (v_Material == 1 || v_Material == 3 || v_Material == 5)
    {
        n = noise(floor(p * 90.0));
    }
    if (v_Material == 1)
    {
        c *= .82 + .3 * n; // soil and grass
    }
    if (v_Material == 2)
    { // irregular wet paving stones
        vec2 q = p * vec2(7, 5);
        q.x += mod(floor(q.y), 2.0) * .5;
        float seam = step(.10, fract(q.x)) * step(.12, fract(q.y));
        c *= mix(.48, .86 + .22 * noise(floor(q)), seam);
    }
    if (v_Material == 3)
    { // timber grain
        c *= .78 + .19 * sin(p.y * 120.0 + sin(p.x * 17.0) * 3.0) + .15 * n;
    }
    if (v_Material == 4)
    { // ceramic roof tiles
        c *= .72 + .3 * sin(p.x * 100.0);
        c *= mix(.55, 1.0, step(.12, fract(p.y * 9.0)));
    }
    if (v_Material == 5)
    { // straw fibers
        c *= .7 + .35 * n + .16 * sin(p.x * 180.0 + p.y * 13.0);
    }
    if (v_Material == 6)
    { // animated water
        float wave = sin(p.x * 30.0 + p.y * 18.0 + u_Time * 1.3);
        c += vec3(.06, .11, .12) * pow(max(wave, 0.0), 12.0);
    }
    if (v_Material == 7)
    {
        a *= 1.0 - smoothstep(.65, 1.0, length((p - .5) * 2.0));
    }
    if (v_Material == 8)
    {
        // One stationary quad; all flame motion is evaluated on the GPU.
        float sway = sin(p.y * 9.0 - u_Time * 7.0) * .065 * p.y;
        float width = mix(.34, .025, p.y);
        float edge = abs(p.x - .5 - sway);
        float flame = 1.0 - smoothstep(width * .45, width, edge);
        flame *= smoothstep(0.0, .09, p.y) * (1.0 - smoothstep(.74, 1.0, p.y));
        float pulse = .85 + .15 * sin(u_Time * 11.0 + p.y * 14.0);
        c = mix(vec3(1.0, .85, .27), vec3(.98, .21, .025), p.y);
        a *= flame * pulse;
    }
    if (v_Material == 9)
    {
        float lift = fract(p.y - u_Time * .17);
        float drift = sin(lift * 7.0 + u_Time * .3) * .12;
        float shape = 1.0 - smoothstep(.12, .46, abs(p.x - .5 - drift));
        a *= shape * smoothstep(0.0, .15, lift) * (1.0 - smoothstep(.5, 1.0, lift));
    }
    FragColor = vec4(c, a);
}
