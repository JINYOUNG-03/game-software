#version 330
in vec2 uv;
out vec4 FragColor;
uniform sampler2D u_Scene;
uniform vec2 u_Pixel;
uniform float u_Time;
void main() {
 vec3 color=texture(u_Scene,uv).rgb, glow=vec3(0);
 for(int x=-2;x<=2;++x) for(int y=-2;y<=2;++y) {
   vec3 c=texture(u_Scene,uv+vec2(x,y)*u_Pixel*4.0).rgb;
   glow+=max(c-vec3(0.66),vec3(0))/25.0;
 }
 color+=glow*0.65;
 color=mix(vec3(dot(color,vec3(.299,.587,.114))),color,.88);
 color*=vec3(.94,1.02,1.04);
 float vignette=1.0-.30*smoothstep(.2,.72,length(uv-.5));
 FragColor=vec4(color*vignette,1);
}
