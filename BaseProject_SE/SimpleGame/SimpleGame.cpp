#include "stdafx.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include "Renderer.h"

namespace {
const int ScreenWidth=1280, ScreenHeight=800, MapSize=28;
const float TileWidth=96, TileHeight=48;
struct Vec3 { float x,y,z; };
struct Npc { const wchar_t* name; Vec3 position; const wchar_t* line; float r,g,b; };
struct Prop { Vec3 position; int kind; };
struct Animal { Vec3 position,home; int kind; float phase; bool moving; };
enum QuestState { FindHealer, FindHerb, ChooseRecipient, Finished };
Renderer* renderer=nullptr;
bool keys[256]={}, interactionPressed=false, questPanel=true, paused=false, moving=false;
bool herbCollected=false, helpedChild=false;
int previousTime=0;
float elapsed=0, walkPhase=0;
Vec3 player={13,12,0};
const Vec3 healerPosition={15,11,0}, herbPosition={5,17,0}, boatmanPosition={18,18,0};
QuestState quest=FindHealer;
std::vector<Prop> props;
std::vector<Animal> animals;
std::wstring speaker, dialogue;
float dialogueTime=0;
HDC fontDC=nullptr;
HFONT fontHandle=nullptr;
std::map<wchar_t,GLuint> glyphs;
Npc villagers[]={
 {L"\uc7a5\uc528", {12,10,0}, L"\uc300\uc740 \uc544\uc9c1 \uc788\uc5b4. \uc57d\uc7ac\uac00 \ubb38\uc81c\uc9c0... \ub4e4\uc5b4\uc624\ub294 \ubc30\uac00 \uc5c6\uac70\ub4e0.",.62f,.39f,.24f},
 {L"\uc21c\ub355 \ud560\uba48", {11,13,0}, L"\uc232\uae38 \uc870\uc2ec\ud574. \uc694 \uba70\uce60 \uc9d0\uc2b9\ub4e4\uc774 \ub9c8\uc744\uae4c\uc9c0 \ub0b4\ub824\uc624\ub354\ub77c.",.42f,.53f,.36f},
 {L"\uc218\ubb38\uc7a5", {14,6,0}, L"\ubd81\ucabd \uae38\uc740 \ub9c9\ud614\uc18c. \ub2f5\ub2f5\ud574\ub3c4 \uc870\uae08\ub9cc \ucc38\uc544 \uc8fc\uc2dc\uc624.",.29f,.37f,.48f},
 {L"\ubcf5\uc774", {10,11,0}, L"\uc6b0\ub9ac \ud615\ub3c4 \ub098\uc73c\uba74 \uac19\uc774 \ub180 \uc218 \uc788\uaca0\uc9c0?",.73f,.49f,.36f},
 {L"\uc8fc\ubaa8", {16,9,0}, L"\uba3c \uae38 \uc654\uc9c0? \uad6d\uc774\ub77c\ub3c4 \ud55c \uadf8\ub987 \uba39\uace0 \uac00.",.56f,.26f,.24f},
 {L"\ub098\ubb34\uafbc", {8,14,0}, L"\uc548\uac1c\ud480 \ucc3e\ub098? \uc232\uae38 \ub05d, \uc816\uc740 \ubc14\uc704 \uc606\uc744 \ubd10.",.37f,.42f,.29f},
 {L"\ub18d\ubd80", {15,16,0}, L"\uc0ac\ub78c\uc774 \uc544\ud30c\ub3c4 \ubc2d\uc740 \ub3cc\ubd10\uc57c\uc9c0. \uc548 \uadf8\ub7ec\uba74 \uaca8\uc6b8\uc744 \ubabb \ub098.",.52f,.44f,.29f},
 {L"\ube68\ub798\ud558\ub294 \uc544\ub099", {17,19,0}, L"\uc800 \ubc43\uc0ac\uacf5 \uc880 \ubd10 \uc918. \uc5b4\uc81c\ubd80\ud130 \ubb3c\ub3c4 \ubabb \ub118\uaca8.",.38f,.47f,.61f},
 {L"\ub3c4\uacf5", {9,9,0}, L"\uae68\uc9c4 \uadf8\ub987 \uc788\uc73c\uba74 \uac00\uc838\uc640. \uc544\uc9c1 \uc190\uc740 \uba40\uca61\ud558\ub2c8\uae4c.",.59f,.44f,.32f},
 {L"\ud589\uc0c1", {13,15,0}, L"\ub3c4\uc131\ub3c4 \ubcc4\uc218 \uc5c6\ub2e4\ub354\uad70. \uc5b4\ub514\ub85c \uac00\uc57c \ud560\uc9c0 \ubaa8\ub974\uaca0\uc5b4.",.46f,.32f,.48f},
 {L"\uc57d\ubc29 \uc2ec\ubd80\ub984\uafbc", {16,12,0}, L"\uc11c\ub9b0 \ub204\ub098\uac00 \ubc24\uc0c8 \ubabb \uc7a4\uc5b4. \uc544\ud508 \uc0ac\ub78c\uc774 \ub108\ubb34 \ub9ce\uc544.",.36f,.54f,.52f},
 {L"\ub178\uc778", {11,7,0}, L"\uc800\ub141\ubc25 \uc9d3\ub294 \uc5f0\uae30\ub294 \uc5ec\uc804\ud55c\ub370... \ub9c8\uc744\uc774 \ucc38 \uc870\uc6a9\ud574\uc84c\uc5b4.",.49f,.49f,.42f}
};
float Distance2D(const Vec3&a,const Vec3&b) { float x=a.x-b.x,y=a.y-b.y; return sqrtf(x*x+y*y); }
bool Water(float x,float y) { float a=(x-23)/5.8f,b=(y-22)/6.4f; return a*a+b*b<1; }
bool Road(float x,float y) {
 return (fabsf(x-13)<.85f && y>4 && y<18) ||
 (fabsf(y-12)<.85f && x>8 && x<19) ||
 (x<10 && x>4 && fabsf(y-(19-x*.5f))<.8f) ||
 (x>13 && x<19 && fabsf(y-x)<.8f);
}
void WorldToScreen(const Vec3&p,float&x,float&y) {
 x=ScreenWidth*.5f+((p.x-p.y)-(player.x-player.y))*TileWidth*.5f;
 y=ScreenHeight*.52f+((p.x+p.y)-(player.x+player.y))*TileHeight*.5f-p.z;
}
bool Visible(float x,float y) { return x>-160 && x<ScreenWidth+160 && y>-100 && y<ScreenHeight+180; }
void InitializeKoreanFont() {
 fontDC=wglGetCurrentDC();
 fontHandle=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,HANGUL_CHARSET,
 OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FF_DONTCARE,L"Malgun Gothic");
}
void DrawText(float x,float y,const wchar_t*text,float r=.93f,float g=.88f,float b=.73f) {
 glUseProgram(0); glColor3f(r,g,b);
 glRasterPos2f(x/ScreenWidth*2-1,1-y/ScreenHeight*2);
 for(const wchar_t*c=text;*c;++c) {
  auto it=glyphs.find(*c);
  if(it==glyphs.end()) {
   GLuint id=glGenLists(1);
   HGDIOBJ previous=SelectObject(fontDC,fontHandle);
   bool ok=id && wglUseFontBitmapsW(fontDC,*c,1,id);
   SelectObject(fontDC,previous);
   if(!ok) { if(id) glDeleteLists(id,1); continue; }
   it=glyphs.insert(std::make_pair(*c,id)).first;
  }
  glCallList(it->second);
 }
}
void Say(const wchar_t*name,const wchar_t*line) { speaker=name; dialogue=line; dialogueTime=9; }
void DrawPrompt(const Vec3&p,const wchar_t*text) {
 float x,y; WorldToScreen(p,x,y);
 renderer->DrawSolidRect(x,y-66,152,28,.06f,.08f,.09f,.9f);
 DrawText(x-65,y-60,text,1,.84f,.44f);
}
	void DrawTree(float x, float y)
	{
		renderer->DrawSolidRect(x, y - 22.0f, 8.0f, 35.0f, 0.23f, 0.16f, 0.10f, 1.0f);
		renderer->DrawSolidDiamond(x, y - 43.0f, 40.0f, 52.0f, 0.11f, 0.25f, 0.18f, 1.0f);
		renderer->DrawSolidDiamond(x - 8.0f, y - 35.0f, 28.0f, 38.0f, 0.16f, 0.33f, 0.22f, 1.0f);
	}


void DrawHouse(float x,float y,bool straw) {
 // Raised foundation, two lit facades and pitched roof planes share one ground anchor.
 renderer->SetMaterial(2);
 renderer->DrawQuad(x-58,y-17,x+10,y+13,x+55,y-12,x-12,y-40,.31f,.34f,.33f);
 renderer->SetMaterial(3);
 renderer->DrawQuad(x-53,y-20,x+10,y+7,x+10,y-49,x-53,y-76,.56f,.46f,.31f);
 renderer->DrawQuad(x+10,y+7,x+51,y-15,x+51,y-71,x+10,y-49,.32f,.31f,.24f);
 // Timber posts and warm paper lattice windows.
 renderer->SetMaterial(0);
 for(int i=0;i<3;++i) {
  float px=x-43+i*19,py=y-41+i*8;
  renderer->DrawQuad(px,py-17,px+13,py-11,px+13,py+12,px,py+6,.77f,.66f,.43f);
  for(int k=0;k<3;++k)
   renderer->DrawSolidRect(px+2+k*4,py-4+k*1.5f,1,22,.28f,.22f,.15f,1);
  renderer->DrawSolidRect(px-5,py-5,3,47,.22f,.17f,.11f,1);
 }
 renderer->SetMaterial(straw?5:4);
 renderer->DrawQuad(x-71,y-73,x-16,y-109,x+67,y-73,x+12,y-39,
   straw?.59f:.22f,straw?.47f:.29f,straw?.26f:.31f);
 renderer->DrawQuad(x-71,y-73,x+12,y-39,x+12,y-33,x-71,y-66,
   straw?.40f:.11f,straw?.31f:.16f,straw?.17f:.18f);
 renderer->DrawQuad(x+12,y-39,x+67,y-73,x+67,y-66,x+12,y-33,
   straw?.35f:.09f,straw?.28f:.13f,straw?.14f:.16f);
 renderer->SetMaterial(0);
 // Roof ridge and upturned end caps.
 renderer->DrawQuad(x-21,y-111,x+61,y-77,x+65,y-80,x-17,y-116,.35f,.40f,.40f);
 renderer->DrawSolidDiamond(x-70,y-72,10,9,.27f,.34f,.35f,1);
 renderer->DrawSolidDiamond(x+68,y-73,10,9,.27f,.34f,.35f,1);
 // Earthenware onggi beside the foundation.
 for(int k=0;k<3;++k) {
  renderer->DrawEllipse(x-65+k*9,y-9+k*4,12,16,.22f,.14f,.09f,1);
  renderer->DrawEllipse(x-65+k*9,y-16+k*4,12,5,.13f,.09f,.06f,1);
 }
}
void DrawGiwaHouse(float x,float y) { DrawHouse(x,y,false); }
void DrawChogaHouse(float x,float y) { DrawHouse(x,y,true); }
	void DrawMarketStall(float x, float y)
	{
		renderer->DrawSolidRect(x, y - 10.0f, 53.0f, 20.0f, 0.33f, 0.20f, 0.12f, 1.0f);
		renderer->DrawSolidDiamond(x, y - 29.0f, 66.0f, 25.0f, 0.48f, 0.16f, 0.12f, 1.0f);
		renderer->DrawSolidRect(x - 20.0f, y - 23.0f, 3.0f, 24.0f, 0.18f, 0.10f, 0.06f, 1.0f);
		renderer->DrawSolidRect(x + 20.0f, y - 23.0f, 3.0f, 24.0f, 0.18f, 0.10f, 0.06f, 1.0f);
	}

	void DrawJangseung(float x, float y)
	{
		renderer->DrawSolidRect(x, y - 28.0f, 11.0f, 50.0f, 0.31f, 0.19f, 0.10f, 1.0f);
		renderer->DrawSolidDiamond(x, y - 55.0f, 18.0f, 20.0f, 0.39f, 0.25f, 0.13f, 1.0f);
		renderer->DrawSolidRect(x, y - 56.0f, 8.0f, 2.0f, 0.08f, 0.05f, 0.03f, 1.0f);
	}

	void DrawLantern(float x, float y)
	{
		float glow = 0.72f + sinf(elapsed * 3.0f + x) * 0.16f;
		renderer->DrawSolidRect(x, y - 19.0f, 3.0f, 26.0f, 0.16f, 0.10f, 0.06f, 1.0f);
		renderer->DrawSolidDiamond(x, y - 33.0f, 12.0f, 17.0f, 1.0f, glow, 0.25f, 0.95f);
	}

	void DrawStoneWell(float x, float y)
	{
		renderer->DrawSolidDiamond(x, y - 8.0f, 33.0f, 17.0f, 0.30f, 0.34f, 0.33f, 1.0f);
		renderer->DrawSolidDiamond(x, y - 13.0f, 23.0f, 11.0f, 0.07f, 0.12f, 0.15f, 1.0f);
		renderer->DrawSolidRect(x, y - 34.0f, 3.0f, 34.0f, 0.19f, 0.13f, 0.08f, 1.0f);
		renderer->DrawSolidRect(x, y - 44.0f, 28.0f, 3.0f, 0.19f, 0.13f, 0.08f, 1.0f);
	}

	void DrawFence(float x, float y, bool horizontal)
	{
		float width = horizontal ? 42.0f : 10.0f;
		float height = horizontal ? 8.0f : 36.0f;
		renderer->DrawSolidRect(x, y - 13.0f, width, height, 0.26f, 0.16f, 0.09f, 1.0f);
		if (horizontal) {
			renderer->DrawSolidRect(x - 16.0f, y - 18.0f, 3.0f, 22.0f, 0.17f, 0.10f, 0.05f, 1.0f);
			renderer->DrawSolidRect(x + 16.0f, y - 18.0f, 3.0f, 22.0f, 0.17f, 0.10f, 0.05f, 1.0f);
		}
	}


void InitializeWorld() {
 // Courtyards leave the main crossroad, forest trail and lake path open.
 for(int x=9;x<=17;x+=4) for(int y=7;y<=15;y+=4)
  if(x!=13) props.push_back({{float(x),float(y),0},(x+y)%3==0?1:0});
 props.push_back({{15,10,0},0});
 props.push_back({{12,9,0},2}); props.push_back({{14,9,0},2});
 props.push_back({{12,13,0},4});
 props.push_back({{12,5,0},3}); props.push_back({{14,5,0},3});
 for(int y=7;y<18;y+=3) props.push_back({{14,float(y),0},5});
 for(int x=9;x<18;x+=2) props.push_back({{float(x),16.5f,0},6});
 for(int x=1;x<27;++x) for(int y=1;y<27;++y) {
  unsigned h=unsigned(x*73856093)^unsigned(y*19349663);
  if((x<8 || y<4 || y>21 || x>20) && !Water(x+.5f,y+.5f) &&
     !Road(x+.5f,y+.5f) && h%4==0 &&
     Distance2D({x+.5f,y+.5f,0},herbPosition)>1.3f)
   props.push_back({{x+.25f+(h%30)*.01f,y+.5f,0},7});
 }
 animals={{{4,11,0},{4,11,0},0,0,false},{{6,20,0},{6,20,0},0,2,false},
 {{3,15,0},{3,15,0},1,1,false},{{20,7,0},{20,7,0},1,4,false},
 {{9,23,0},{9,23,0},2,3,false},{{18,4,0},{18,4,0},2,5,false}};
}
bool IsWalkable(float x,float y) {
 if(x<.4f||y<.4f||x>MapSize-.4f||y>MapSize-.4f||Water(x,y)) return false;
 for(const auto&p:props) {
  float radius=p.kind<=1?.70f:p.kind==7?.22f:p.kind==4?.32f:0;
  if(radius && Distance2D({x,y,0},p.position)<radius) return false;
 }
 return true;
}
void DrawActor(const Vec3&p,float r,float g,float b,bool hero) {
 float x,y; WorldToScreen(p,x,y);
 float stride=hero&&moving?sinf(walkPhase)*3:0;
 float bob=hero&&moving?fabsf(sinf(walkPhase))*1.4f:sinf(elapsed*2+p.x)*.5f;
 y-=bob;
 // Layered hanbok silhouette: shoes, trousers, robe, sleeves, collar, head and gat.
 renderer->DrawSolidRect(x-4,y-4+stride,6,9,.12f,.13f,.13f,1);
 renderer->DrawSolidRect(x+4,y-4-stride,6,9,.12f,.13f,.13f,1);
 renderer->DrawSolidDiamond(x,y-16,23,28,r*.7f,g*.7f,b*.7f,1);
 renderer->DrawSolidRect(x,y-25,15,17,r,g,b,1);
 renderer->DrawSolidRect(x-11,y-23-stride,7,16,r*.85f,g*.85f,b*.85f,1);
 renderer->DrawSolidRect(x+11,y-23+stride,7,16,r*.85f,g*.85f,b*.85f,1);
 renderer->DrawSolidDiamond(x,y-28,9,12,.85f,.81f,.66f,1);
 renderer->DrawSolidRect(x,y-20,16,3,.19f,.17f,.13f,1);
 renderer->DrawEllipse(x,y-40,13,16,.70f,.53f,.36f,1);
 renderer->DrawSolidRect(x+3,y-40,2,2,.12f,.09f,.08f,1);
 renderer->DrawEllipse(x,y-47,hero?31.0f:18.0f,7,.08f,.10f,.12f,1);
 renderer->DrawSolidRect(x,y-51,hero?14.0f:10.0f,8,.09f,.12f,.14f,1);
 if(hero) renderer->DrawSolidRect(x+15,y-19,3,26,.28f,.23f,.19f,1);
}
void DrawAnimal(const Animal&a) {
 float x,y; WorldToScreen(a.position,x,y);
 float step=a.moving?sinf(a.phase*9)*4:0;
 float c=a.kind==0?.67f:a.kind==1?.25f:.43f;
 for(int i=0;i<4;++i)
  renderer->DrawSolidRect(x-12+i*8,y-5+((i%2)?step:-step),3,a.kind==0?19.0f:10.0f,c*.65f,c*.5f,c*.4f,1);
 renderer->DrawEllipse(x,y-18,35,20,c,c*.8f,c*.6f,1);
 renderer->DrawEllipse(x+17,y-25,16,16,c,c*.82f,c*.66f,1);
 renderer->DrawSolidDiamond(x+12,y-36,7,15,c,c*.8f,c*.6f,1);
 renderer->DrawSolidDiamond(x+21,y-35,6,13,c,c*.8f,c*.6f,1);
 renderer->DrawSolidRect(x+21,y-26,2,2,.06f,.05f,.04f,1);
 if(a.kind==0) {
  renderer->DrawSolidRect(x+12,y-42,2,17,.45f,.34f,.22f,1);
  renderer->DrawSolidRect(x+19,y-42,2,17,.45f,.34f,.22f,1);
 }
}
void RenderTerrain() {
 for(int sum=0;sum<MapSize*2-1;++sum) for(int x=0;x<MapSize;++x) {
  int y=sum-x; if(y<0||y>=MapSize) continue;
  float sx,sy; WorldToScreen({x+.5f,y+.5f,0},sx,sy); if(!Visible(sx,sy)) continue;
  bool water=Water(x+.5f,y+.5f),road=Road(x+.5f,y+.5f);
  bool field=x>=10&&x<=16&&y>=17&&y<=19;
  float n=((x*17+y*31)%9)*.006f;
  renderer->SetMaterial(water?6:road?2:1);
  renderer->DrawSolidDiamond(sx,sy,TileWidth+1,TileHeight+1,
   water?.06f:road?.39f:field?.31f:.18f+n,
   water?.22f:road?.36f:field?.34f:.28f+n,
   water?.28f:road?.28f:field?.13f:.19f+n,1);
  renderer->SetMaterial(0);
  if(field&&!water&&!road) for(int i=0;i<5;++i)
   renderer->DrawSolidDiamond(sx+(i-2)*12,sy,4,17,.45f,.47f,.18f,1);
  if(!water&&!road&&!field) for(int i=0;i<3;++i)
   renderer->DrawSolidDiamond(sx+(i-1)*14,sy-3+i*3,3,9,.25f,.36f,.21f,.6f);
 }
 // A timber landing sits on the dry western bank.
 float x,y; WorldToScreen(boatmanPosition,x,y);
 renderer->SetMaterial(3);
 renderer->DrawSolidDiamond(x+20,y+12,92,45,.38f,.28f,.16f,1);
 renderer->SetMaterial(0);
}
void RenderWorldObjects() {
 struct Item { Vec3 p; int kind,index; };
 std::vector<Item> items;
 for(int i=0;i<int(props.size());++i) items.push_back({props[i].position,0,i});
 for(int i=0;i<int(sizeof(villagers)/sizeof(Npc));++i) items.push_back({villagers[i].position,1,i});
 items.push_back({healerPosition,2,0}); items.push_back({boatmanPosition,2,1});
 items.push_back({player,3,0});
 for(int i=0;i<int(animals.size());++i) items.push_back({animals[i].position,4,i});
 if(!herbCollected) items.push_back({herbPosition,5,0});
 // Soft directional ground shadows are drawn before the opaque silhouettes.
 for(const auto&i:items) {
  float x,y; WorldToScreen(i.p,x,y); if(!Visible(x,y)) continue;
  float w=i.kind==0?(props[i.index].kind<=1?105.0f:48.0f):30.0f;
  renderer->DrawEllipse(x+14,y+5,w,22,.025f,.045f,.055f,.30f);
  renderer->DrawEllipse(x+3,y+1,w*.55f,12,.02f,.03f,.035f,.36f);
 }
 std::stable_sort(items.begin(),items.end(),[](const Item&a,const Item&b){return a.p.x+a.p.y<b.p.x+b.p.y;});
 for(const auto&i:items) {
  float x,y; WorldToScreen(i.p,x,y); if(!Visible(x,y)) continue;
  renderer->SetMaterial(0);
  if(i.kind==0) {
   switch(props[i.index].kind) {
    case 0: DrawGiwaHouse(x,y); break; case 1: DrawChogaHouse(x,y); break;
    case 2: DrawMarketStall(x,y); break; case 3: DrawJangseung(x,y); break;
    case 4: DrawStoneWell(x,y); break; case 5: DrawLantern(x,y); break;
    case 6: DrawFence(x,y,true); break;
    case 7:
     DrawTree(x,y);
     renderer->SetMaterial(0);
     for(int k=0;k<3;++k) renderer->DrawEllipse(x+(k-1)*15,y-52-k*9,47,35,.12f+k*.018f,.25f+k*.02f,.19f,1);
     break;
   }
  } else if(i.kind==1) { const auto&n=villagers[i.index]; DrawActor(n.position,n.r,n.g,n.b,false); }
  else if(i.kind==2) DrawActor(i.p,i.index?.40f:.70f,.54f,.46f,false);
  else if(i.kind==3) DrawActor(player,.38f,.46f,.60f,true);
  else if(i.kind==4) DrawAnimal(animals[i.index]);
  else {
   renderer->DrawEllipse(x,y-8,44,22,.13f,.66f,.54f,.28f);
   for(int k=0;k<4;++k) renderer->DrawSolidDiamond(x+(k-2)*5,y-9,7,22,.36f,.76f,.61f,1);
  }
 }
 renderer->SetMaterial(0);
}
void RenderEffects() {
 for(const auto&p:props) {
  float x,y; WorldToScreen(p.position,x,y); if(!Visible(x,y)) continue;
  if(p.kind==5) {
   renderer->DrawEllipse(x,y-30,95,80,1,.52f,.13f,.08f);
   for(int k=0;k<5;++k) {
    float t=fmodf(elapsed*1.3f+k*.2f,1);
    renderer->DrawEllipse(x+sinf(t*8+k)*4,y-30-t*25,9*(1-t)+2,15*(1-t)+2,1,.35f+t*.5f,.09f,1-t);
   }
  }
  if(p.kind<=1) for(int k=0;k<4;++k) {
   float t=fmodf(elapsed*.15f+k*.25f,1);
   renderer->DrawEllipse(x+26+t*35,y-65-t*50,18+t*35,13+t*22,.47f,.52f,.50f,(1-t)*.13f);
  }
 }
 for(int k=0;k<12;++k) {
  Vec3 p={20.0f+(k%4)*2,18.0f+(k/4)*2,0};
  float x,y; WorldToScreen(p,x,y);
  renderer->DrawEllipse(x+sinf(elapsed*.13f+k)*35,y,230,65,.50f,.64f,.65f,.045f);
 }
}
const wchar_t* QuestText() {
 switch(quest) {
 case FindHealer:return L"\uc57d\ubc29\uc5d0 \ubd88\uc774 \ucf1c\uc838 \uc788\ub2e4. \uc11c\ub9b0\uc5d0\uac8c \uac00 \ubcf4\uc790.";
 case FindHerb:return L"\uc11c\ucabd \uc232\uae38 \ub05d\uc5d0\uc11c \uc548\uac1c\ud480\uc744 \ucc3e\uc544\ubcf4\uc790.";
 case ChooseRecipient:return L"\uc57d\uc740 \ud55c \uc0ac\ub78c \ubaab\ubfd0\uc774\ub2e4. \uc11c\ub9b0\uacfc \ubc43\uc0ac\uacf5, \ub204\uad6c\uc5d0\uac8c \uc904\uae4c?";
 default:return helpedChild?L"\uc544\uc774\uc758 \uc5f4\uc740 \ub0b4\ub838\ub2e4. \ud638\uc22b\uac00 \uc0ac\ub78c\uc774 \ub9c8\uc74c\uc5d0 \uac78\ub9b0\ub2e4.":L"\ubc43\uc0ac\uacf5\uc740 \uace0\ube44\ub97c \ub118\uacbc\ub2e4. \uc57d\ubc29\uc73c\ub85c\ub294 \ubc1c\uc774 \ub5a8\uc5b4\uc9c0\uc9c0 \uc54a\ub294\ub2e4.";
 }
}
void TryInteract() {
 if(Distance2D(player,healerPosition)<1.3f) {
  if(quest==FindHealer) {quest=FindHerb; Say(L"\uc11c\ub9b0",L"\uc544\uc774 \uc5f4\uc774 \uc548 \ub0b4\ub824\uc694. \uc11c\ucabd \uc232\uc5d0 \uc548\uac1c\ud480\uc774 \uc788\uc5b4\uc694. \uc870\uae08\ub9cc \uad6c\ud574 \uc8fc\uc2e4\ub798\uc694?");}
  else if(quest==ChooseRecipient) {quest=Finished; helpedChild=true; Say(L"\uc11c\ub9b0",L"\uace0\ub9c8\uc6cc\uc694... \uc774\uc81c \uc228\uc774 \uc880 \uace0\ub974\ub124\uc694. \ud638\uc22b\uac00 \ubd84\ub3c4 \uc57d\uc744 \uae30\ub2e4\ub9ac\uc168\ub2e4\ub294\ub370...");}
  else Say(L"\uc11c\ub9b0",quest==Finished?L"\uc624\ub298\uc740 \ubc84\ud17c\uc5b4\uc694. \ub0b4\uc77c\ub3c4 \uadf8\ub7ac\uc73c\uba74 \uc88b\uaca0\ub124\uc694.":L"\uc232\uae38 \ub05d \uc816\uc740 \ubc14\uc704 \uc606\uc774\uc5d0\uc694. \uc870\uc2ec\ud574\uc11c \ub2e4\ub140\uc624\uc138\uc694.");
  return;
 }
 if(Distance2D(player,herbPosition)<1.3f&&quest==FindHerb) {
  herbCollected=true; quest=ChooseRecipient;
  Say(L"\uc548\uac1c\ud480",L"\uba40\uca61\ud55c \uac74 \uc774 \ud55c \uc90c\ubfd0\uc774\ub2e4. \uc544\uc774\ub3c4, \ubc43\uc0ac\uacf5\ub3c4 \uc774 \uc57d\uc774 \ud544\uc694\ud558\ub2e4."); return;
 }
 if(Distance2D(player,boatmanPosition)<1.3f) {
  if(quest==ChooseRecipient) {quest=Finished; helpedChild=false; Say(L"\ubc43\uc0ac\uacf5",L"\uc0b4\uc558\uad6c\uba3c... \uc774 \uc740\ud61c\ub97c \uc5b4\ucc0c \uac1a\ub098. \uadf8\ub7f0\ub370 \uadf8 \uc544\uc774\ub294... \uad1c\ucc2e\uc740 \uac74\uac00?");}
  else Say(L"\ubc43\uc0ac\uacf5",quest==Finished&&!helpedChild?L"\ub355\ubd84\uc5d0 \uc228\uc740 \uc26c\uaca0\ub124. \uace0\ub9d9\uc18c.":L"\ubb3c \ud55c \ubaa8\uae08\ub9cc... \uc57d\ubc29\uc5d0 \uc57d\uc774 \ub0a8\uc558\ub294\uc9c0 \uc880 \ubd10 \uc8fc\uaca0\uc18c?");
  return;
 }
 int nearest=-1; float distance=1.3f;
 for(int i=0;i<int(sizeof(villagers)/sizeof(Npc));++i) {
  float d=Distance2D(player,villagers[i].position);
  if(d<distance) {distance=d;nearest=i;}
 }
 if(nearest>=0) Say(villagers[nearest].name,villagers[nearest].line);
}
void RenderMiniMap() {
 const float cx=1150,cy=126,scale=3.3f;
 renderer->DrawSolidRect(cx,cy,240,225,.04f,.07f,.075f,.94f);
 for(int x=0;x<MapSize;++x) for(int y=0;y<MapSize;++y) {
  bool w=Water(x+.5f,y+.5f),r=Road(x+.5f,y+.5f);
  renderer->DrawSolidDiamond(cx+(x-y)*scale,cy+(x+y-MapSize)*scale*.5f,scale*2+1,scale+1,
    w?.09f:r?.55f:.22f,w?.28f:r?.46f:.34f,w?.38f:.21f,1);
 }
 auto marker=[&](const Vec3&p,float r,float g,float b,float size) {
  renderer->DrawEllipse(cx+(p.x-p.y)*scale,cy+(p.x+p.y-MapSize)*scale*.5f,size,size,r,g,b,1);
 };
 for(const auto&p:props) if(p.kind<=1) marker(p.position,.62f,.49f,.35f,4);
 for(const auto&n:villagers) marker(n.position,.56f,.72f,.56f,3);
 for(const auto&a:animals) marker(a.position,.86f,.43f,.27f,3);
 if(quest!=Finished) {
  marker(quest==FindHerb?herbPosition:healerPosition,1,.79f,.19f,7);
  if(quest==ChooseRecipient) marker(boatmanPosition,1,.79f,.19f,7);
 }
 marker(player,.95f,.96f,1,6);
 DrawText(cx-101,cy-92,L"\uc548\uac1c\ud638 \uc77c\ub300",.88f,.77f,.53f);
 DrawText(cx-104,cy+96,L"\ud770\uc0c9 \ub098 / \uae08\uc0c9 \ubaa9\uc801\uc9c0",.8f,.82f,.75f);
}
void RenderUI() {
 renderer->SetMaterial(0);
 renderer->DrawSolidRect(370,45,700,58,.035f,.055f,.06f,.90f);
 DrawText(35,40,L"\uc548\uac1c\ud638  |  \ud574 \uc9c8 \ub158",.92f,.76f,.48f);
 if(questPanel) DrawText(35,66,QuestText());
 DrawText(28,777,L"WASD \uc774\ub3d9   E \ub9d0 \uac78\uae30 / \ucc44\uc9d1   TAB \ud560 \uc77c   ESC \uc26c\uc5b4\uac00\uae30",.8f,.81f,.73f);
 if(dialogueTime>0) {
  renderer->DrawSolidRect(540,701,1030,94,.035f,.05f,.055f,.95f);
  DrawText(42,682,speaker.c_str(),.97f,.77f,.41f);
  DrawText(42,715,dialogue.c_str());
 }
 RenderMiniMap();
 if(Distance2D(player,healerPosition)<1.3f) DrawPrompt(healerPosition,L"E \uc11c\ub9b0");
 else if(Distance2D(player,boatmanPosition)<1.3f) DrawPrompt(boatmanPosition,L"E \ubc43\uc0ac\uacf5");
 else if(quest==FindHerb&&Distance2D(player,herbPosition)<1.3f) DrawPrompt(herbPosition,L"E \uc548\uac1c\ud480 \ub530\uae30");
 else for(const auto&n:villagers) if(Distance2D(player,n.position)<1.3f) {DrawPrompt(n.position,L"E \ub9d0 \uac78\uae30");break;}
 if(paused) {
  renderer->DrawSolidRect(640,400,390,90,.03f,.05f,.06f,.96f);
  DrawText(498,405,L"\uc7a0\uc2dc \uc26c\ub294 \uc911 - ESC\ub85c \ub3cc\uc544\uac00\uae30");
 }
}
void Update(float dt) {
 if(paused) {interactionPressed=false;return;}
 elapsed+=dt; dialogueTime-=dt;
 float sx=(keys['d']?1.0f:0)-(keys['a']?1.0f:0),sy=(keys['s']?1.0f:0)-(keys['w']?1.0f:0);
 float len=sqrtf(sx*sx+sy*sy);
 moving=len>0;
 if(moving) {
  // Inverse isometric projection gives equal speed for screen-cardinal inputs.
  sx=sx/len*145*dt; sy=sy/len*145*dt;
  float dx=sx/TileWidth+sy/TileHeight,dy=sy/TileHeight-sx/TileWidth;
  if(IsWalkable(player.x+dx,player.y)) player.x+=dx;
  if(IsWalkable(player.x,player.y+dy)) player.y+=dy;
  walkPhase+=dt*10;
 }
 for(auto&a:animals) {
  a.phase+=dt;
  float d=Distance2D(a.position,player),vx,vy;
  if(d<3.5f) {vx=(a.position.x-player.x)/(d+.01f)*2;vy=(a.position.y-player.y)/(d+.01f)*2;}
  else {vx=(a.home.x+sinf(a.phase*.4f)*1.5f-a.position.x)*.45f;vy=(a.home.y+cosf(a.phase*.3f)*1.5f-a.position.y)*.45f;}
  a.moving=fabsf(vx)+fabsf(vy)>.1f;
  if(IsWalkable(a.position.x+vx*dt,a.position.y)) a.position.x+=vx*dt;
  if(IsWalkable(a.position.x,a.position.y+vy*dt)) a.position.y+=vy*dt;
 }
 if(interactionPressed) {TryInteract();interactionPressed=false;}
}
}
void RenderScene() {
 if(!renderer)return;
 renderer->SetTime(elapsed);
 renderer->BeginFrame(.075f,.12f,.14f,1);
 RenderTerrain(); RenderWorldObjects(); RenderEffects();
 renderer->EndWorld(elapsed);
 RenderUI(); glutSwapBuffers();
}
void Idle() {
 int now=glutGet(GLUT_ELAPSED_TIME);
 float dt=previousTime?float(now-previousTime)/1000:0;
 previousTime=now; Update((std::min)(dt,.05f)); glutPostRedisplay();
}
void KeyDown(unsigned char key,int,int) {
 if(key>='A'&&key<='Z')key+=32;
 if(keys[key])return;
 keys[key]=true;
 if(key==27)paused=!paused;
 else if(key=='\t')questPanel=!questPanel;
 else if(key=='e'&&!paused)interactionPressed=true;
}
void KeyUp(unsigned char key,int,int) {
 if(key>='A'&&key<='Z')key+=32;
 keys[key]=false;
}
void Reshape(int w,int h) {
 if(renderer) renderer->SetOutputSize(w>0?w:1,h>0?h:1);
}
void Close() {
 for(const auto&g:glyphs)glDeleteLists(g.second,1);
 glyphs.clear();
 delete renderer; renderer=nullptr;
}
int main(int argc,char**argv) {
 glutInit(&argc,argv);
 glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGBA);
 glutInitWindowSize(ScreenWidth,ScreenHeight);
 glutCreateWindow("Mist Lake");
 SetWindowTextW(GetActiveWindow(),L"\uc548\uac1c\ud638 \ub9c8\uc744");
 if(glewInit()!=GLEW_OK||!glewIsSupported("GL_VERSION_3_3"))return 1;
 glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
 renderer=new Renderer(ScreenWidth,ScreenHeight);
 if(!renderer->IsInitialized()){delete renderer;return 1;}
 InitializeKoreanFont(); InitializeWorld();
 Say(L"\uc548\uac1c\ud638",L"\uc800\ub141 \uc5f0\uae30\uac00 \ud53c\uc5b4\uc624\ub978\ub2e4. \uc57d\ubc29\uc5d0\uc11c\ub294 \uc544\uc9c1 \ubd88\uc744 \ub044\uc9c0 \ubabb\ud588\ub2e4.");
 glutIgnoreKeyRepeat(1);
 glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE,GLUT_ACTION_GLUTMAINLOOP_RETURNS);
 glutReshapeFunc(Reshape); glutCloseFunc(Close);
 glutDisplayFunc(RenderScene); glutIdleFunc(Idle); glutKeyboardFunc(KeyDown); glutKeyboardUpFunc(KeyUp);
 glutMainLoop();
 // The native close callback may already have destroyed the context.
 if(renderer && wglGetCurrentContext()) Close();
 if(fontHandle)DeleteObject(fontHandle);
 return 0;
}
