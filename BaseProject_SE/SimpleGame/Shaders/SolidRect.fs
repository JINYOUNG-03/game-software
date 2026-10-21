#version 330
layout(location = 0) out vec4 FragColor;
flat in vec4 v_Color;
flat in vec4 v_Surface;
flat in int v_Material;
in vec2 v_TexCoord;
in vec2 v_Local;
in vec2 v_World;
in vec2 v_Screen;
uniform sampler2D u_Texture;
uniform float u_Time;
uniform bool u_WorldShading;
uniform int u_LightCount;
uniform vec4 u_Lights[8]; // screen x/y, radius, strength; bounded, no new pass per light

float hash(vec2 p)
{
    p = fract(p * vec2(.1031, .11369));
    p += dot(p, p.yx + 33.33);
    return fract((p.x + p.y) * p.x);
}

float noise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}

float edgeDistance(vec2 cell, int mask)
{
    float d = 2.0;
    if ((mask & 1) != 0) d = min(d, cell.x);
    if ((mask & 2) != 0) d = min(d, 1.0 - cell.x);
    if ((mask & 4) != 0) d = min(d, cell.y);
    if ((mask & 8) != 0) d = min(d, 1.0 - cell.y);
    return d;
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
        if (cached.a > .0001) cached.rgb /= cached.a;
        FragColor = cached * v_Color;
        return;
    }

    vec3 c = v_Color.rgb;
    float a = v_Color.a;
    // Resource PNGs use straight RGBA and top-down rows (not the flipped minimap FBO).
    if (v_Material >= 22 && v_Material <= 27)
    {
        vec4 sprite = texture(u_Texture, v_TexCoord);
        c *= sprite.rgb;
        a *= sprite.a;
        if (a < .003) discard;
    }
    vec2 p = v_Local;
    // Invert the diamond's local axes for neighbour masks, even if a parent moves the terrain root.
    // Waves/noise remain continuous in v_World; only the shoreline distance is tile-local.
    vec2 cell = clamp(vec2(p.x - p.y + .5, 1.5 - p.x - p.y), 0.0, 1.0);
    float broad = noise(v_World * .23);
    if (v_Material == 1 || v_Material == 23)
    {
        float meadow = noise(v_World * .72);
        if (v_Material == 1) c = mix(vec3(.19, .29, .21), vec3(.32, .40, .26), meadow);
        c *= .88 + .18 * broad;
        float roadEdge = 1.0 - smoothstep(.01, .26, edgeDistance(cell, int(v_Surface.y)));
        float bank = 1.0 - smoothstep(.02, .32, edgeDistance(cell, int(v_Surface.x)));
        c = mix(c, vec3(.38, .34, .25), roadEdge * .68);
        c = mix(c, vec3(.23, .28, .24), bank * .78);
        float flecks = smoothstep(.91, .98, hash(floor(v_World * 28.0)));
        c += vec3(.045, .054, .02) * flecks;
    }
    else if (v_Material == 2 || v_Material == 24)
    {
        vec2 q = u_WorldShading && v_Surface.z >= 0.0
                 ? v_World * 2.6 : p * vec2(7, 5);
        // Non-terrain masonry keeps local texture; terrain has a nonnegative depth sentinel.
        q.x += mod(floor(q.y), 2.0) * .5;
        vec2 f = fract(q);
        float seam = smoothstep(.02, .10, min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)));
        c *= mix(.64, .96 + .12 * hash(floor(q)), seam);
        float grassEdge = 1.0 - smoothstep(.01, .17, edgeDistance(cell, int(v_Surface.y)));
        c = mix(c, vec3(.26, .32, .24), grassEdge * .68);
        float wet = smoothstep(.62, .81, noise(v_World * .85 + 7.0));
        c = mix(c, vec3(.25, .33, .32), wet * (.26 + .35 * v_Surface.w));
        float glint = pow(max(sin(v_World.x * .9 + v_World.y * 1.8), 0.0), 18.0);
        c += vec3(.08, .10, .09) * wet * glint;
    }
    else if (v_Material == 3)
    {
        float grain = noise(vec2(p.x * 8.0, p.y * 64.0));
        c *= .80 + .27 * grain;
        c *= .85 + .15 * smoothstep(.05, .25, p.y);
    }
    else if (v_Material == 4)
    {
        vec2 q = p * vec2(17, 7);
        float channel = .5 + .5 * cos(q.x * 6.28318);
        float lip = smoothstep(.02, .14, fract(q.y));
        c *= (.72 + .31 * channel) * mix(.62, 1.0, lip);
        c += vec3(.08, .11, .11) * pow(channel, 6.0) * .36;
        c *= mix(.72, 1.03, smoothstep(0.0, 1.0, p.y));
    }
    else if (v_Material == 5)
    {
        float fibers = noise(vec2(p.x * 100.0, p.y * 8.0));
        c *= .76 + .28 * fibers;
        c *= .82 + .18 * p.y;
    }
    else if (v_Material == 6 || v_Material == 25)
    {
        float depth = clamp(v_Surface.z, 0.0, 1.0);
        float shore = edgeDistance(cell, int(v_Surface.x));
        vec3 shallow = vec3(.22, .40, .37);
        vec3 deep = vec3(.075, .24, .29);
        c = mix(shallow, deep, smoothstep(.05, .95, depth));
        c = mix(c, vec3(.24, .29, .24), v_Surface.w * .72);
        float swell = sin(v_World.x * 2.0 + v_World.y * 1.45 - u_Time * .74);
        float ripple = sin(v_World.x * 5.3 - v_World.y * 3.7 + u_Time * 1.08 + swell * .5);
        c += vec3(.04, .07, .07) * swell * .24;
        float sheen = pow(max(ripple, 0.0), 22.0);
        c += vec3(.20, .25, .21) * sheen * (.25 + .4 * broad);
        float foamLine = .065 + sin(u_Time * 1.1 + v_World.x * 2.1) * .018;
        float foam = (1.0 - smoothstep(.012, .036, abs(shore - foamLine))) *
                     (.4 + .6 * noise(v_World * 6.0));
        c = mix(c, vec3(.62, .69, .58), foam * .48);
        c = mix(vec3(.26, .35, .31), c, smoothstep(0.0, .06, shore));
        vec2 rainCell = floor(v_World * 1.6);
        float cycle = fract(u_Time * .32 + hash(rainCell));
        float ring = length(fract(v_World * 1.6) - vec2(.5));
        float rain = (1.0 - smoothstep(.008, .024, abs(ring - cycle * .42))) * (1.0 - cycle);
        c += vec3(.06, .09, .085) * rain;
    }
    else if (v_Material == 7)
    {
        a *= 1.0 - smoothstep(.65, 1.0, length((p - .5) * 2.0));
    }
    else if (v_Material == 8)
    {
        float sway = sin(p.y * 9.0 - u_Time * 7.0) * .065 * p.y;
        float width = mix(.34, .025, p.y);
        float flame = 1.0 - smoothstep(width * .45, width, abs(p.x - .5 - sway));
        a *= flame * smoothstep(0.0, .09, p.y) * (1.0 - smoothstep(.74, 1.0, p.y));
        c = mix(vec3(1.0, .87, .38), vec3(.97, .31, .08), p.y);
    }
    else if (v_Material == 9)
    {
        float lift = fract(p.y - u_Time * .12);
        float drift = sin(lift * 7.0 + u_Time * .3) * .12;
        a *= (1.0 - smoothstep(.12, .46, abs(p.x - .5 - drift))) *
             smoothstep(0.0, .15, lift) * (1.0 - smoothstep(.5, 1.0, lift));
    }
    else if (v_Material == 12) // aged lime/earthen plaster
    {
        c *= .90 + .12 * noise(p * 18.0);
        c *= mix(.67, 1.0, smoothstep(.02, .38, p.y));
        float moss = (1.0 - smoothstep(.08, .30, p.y)) * noise(p * 9.0);
        c = mix(c, vec3(.24, .31, .22), moss * .44);
    }
    else if (v_Material == 13) // cutout foliage; cached geometry, GPU wind
    {
        float r = length((p - .5) * 2.0);
        float edge = .86 + noise(p * 8.0) * .13;
        a *= 1.0 - smoothstep(edge - .055, edge, r);
        c *= .78 + .22 * p.y + .13 * noise(p * 7.0);
    }
    else if (v_Material == 14) // fabric/medicine flags
    {
        c *= .88 + .10 * sin(p.x * 27.0 + u_Time * .9) + .06 * noise(p * 45.0);
    }
    else if (v_Material == 16) // drifting mist, no particle simulation
    {
        float shape = 1.0 - smoothstep(.26, 1.0, length((p - .5) * vec2(2, 2.8)));
        a *= shape * (.4 + .6 * noise(vec2(p.x * 4.0 - u_Time * .04, p.y * 3.0)));
    }
    else if (v_Material == 17) // local warm halo
    {
        a *= pow(max(0.0, 1.0 - dot((p - .5) * 2.0, (p - .5) * 2.0)), 2.0);
    }
    else if (v_Material == 19) // directional soft silhouette
    {
        a *= smoothstep(0.0, .16, p.x) * (1.0 - smoothstep(.66, 1.0, p.x));
        a *= smoothstep(0.0, .15, p.y) * (1.0 - smoothstep(.80, 1.0, p.y));
    }

    if (v_Material == 20) // matte hanbok fabric, shaded without expensive extra geometry
    {
        c *= .87 + .12 * p.y + .045 * noise(p * vec2(26, 40));
    }
    else if (v_Material == 21) // soft face silhouette and cheek shading
    {
        a *= 1.0 - smoothstep(.88, 1.0, length((p - .5) * vec2(2.05, 2.0)));
        c *= (.87 + .13 * p.y) * (1.0 - .10 * smoothstep(.45, 1.0, p.x));
    }

    if (u_WorldShading && v_Material != 8 && v_Material != 16 && v_Material != 17 && v_Material != 19)
    {
        c *= vec3(.91, .96, 1.0);
        for (int i = 0; i < 8; ++i)
        {
            if (i >= u_LightCount) break;
            vec2 d = (v_Screen - u_Lights[i].xy) / max(u_Lights[i].z, 1.0);
            float light = max(0.0, 1.0 - dot(d, d));
            c += vec3(.42, .24, .085) * light * light * u_Lights[i].w;
        }
    }
    FragColor = vec4(c, a);
}
