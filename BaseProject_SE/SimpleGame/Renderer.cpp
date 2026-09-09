#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "Renderer.h"

Renderer::Renderer(int windowSizeX, int windowSizeY) { Initialize(windowSizeX, windowSizeY); }

Renderer::~Renderer()
{
	if (m_QuadBuffer) glDeleteBuffers(1, &m_QuadBuffer);
	if (m_Framebuffer) glDeleteFramebuffers(1, &m_Framebuffer);
	if (m_SceneTexture) glDeleteTextures(1, &m_SceneTexture);
	if (m_PostShader) glDeleteProgram(m_PostShader);
	if (m_VBORect != 0) glDeleteBuffers(1, &m_VBORect);
	if (m_VBODiamond != 0) glDeleteBuffers(1, &m_VBODiamond);
	if (m_SolidRectShader != 0) glDeleteProgram(m_SolidRectShader);
}

void Renderer::Initialize(int windowSizeX, int windowSizeY)
{
	m_WindowSizeX = windowSizeX; m_WindowSizeY = windowSizeY;
	m_OutputWidth = windowSizeX; m_OutputHeight = windowSizeY;
	m_SolidRectShader = CompileShaders("./Shaders/SolidRect.vs", "./Shaders/SolidRect.fs");
	m_PostShader = CompileShaders("./Shaders/Post.vs", "./Shaders/Post.fs");
	glGenTextures(1, &m_SceneTexture);
	glBindTexture(GL_TEXTURE_2D, m_SceneTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, windowSizeX, windowSizeY, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenFramebuffers(1, &m_Framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_SceneTexture, 0);
	bool targetReady = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	CreateVertexBufferObjects();
	m_Initialized = targetReady && m_PostShader > 0 && m_SolidRectShader > 0 && m_VBORect > 0 && m_VBODiamond > 0;
}

bool Renderer::IsInitialized() { return m_Initialized; }

void Renderer::BeginFrame(float r, float g, float b, float a)
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
	glViewport(0, 0, m_WindowSizeX, m_WindowSizeY);
	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::CreateVertexBufferObjects()
{
	glGenBuffers(1, &m_QuadBuffer);
	const float rect[] = { -0.5f,-0.5f,0, -0.5f,0.5f,0, 0.5f,0.5f,0, -0.5f,-0.5f,0, 0.5f,0.5f,0, 0.5f,-0.5f,0 };
	const float diamond[] = { 0,-0.5f,0, -0.5f,0,0, 0,0.5f,0, 0,-0.5f,0, 0,0.5f,0, 0.5f,0,0 };
	glGenBuffers(1, &m_VBORect); glBindBuffer(GL_ARRAY_BUFFER, m_VBORect);
	glBufferData(GL_ARRAY_BUFFER, sizeof(rect), rect, GL_STATIC_DRAW);
	glGenBuffers(1, &m_VBODiamond); glBindBuffer(GL_ARRAY_BUFFER, m_VBODiamond);
	glBufferData(GL_ARRAY_BUFFER, sizeof(diamond), diamond, GL_STATIC_DRAW);
}

void Renderer::DrawSolidRect(float x, float y, float width, float height, float r, float g, float b, float a)
{
	DrawShape(m_VBORect, x, y, width, height, r, g, b, a);
}

void Renderer::DrawSolidDiamond(float x, float y, float width, float height, float r, float g, float b, float a)
{
	DrawShape(m_VBODiamond, x, y, width, height, r, g, b, a);
}

void Renderer::DrawShape(GLuint vertexBuffer, float x, float y, float width, float height, float r, float g, float b, float a)
{
	if (!m_Initialized) return;
	const float clipX = (x / m_WindowSizeX) * 2.0f - 1.0f;
	const float clipY = 1.0f - (y / m_WindowSizeY) * 2.0f;
	const float clipWidth = (width / m_WindowSizeX) * 2.0f;
	const float clipHeight = (height / m_WindowSizeY) * 2.0f;
	glUseProgram(m_SolidRectShader);
	glUniform1i(glGetUniformLocation(m_SolidRectShader, "u_Material"), m_Material);
	glUniform1f(glGetUniformLocation(m_SolidRectShader, "u_Time"), m_Time);
	glUniform4f(glGetUniformLocation(m_SolidRectShader, "u_Transform"), clipX, clipY, clipWidth, clipHeight);
	glUniform4f(glGetUniformLocation(m_SolidRectShader, "u_Color"), r, g, b, a);
	const GLint position = glGetAttribLocation(m_SolidRectShader, "a_Position");
	glEnableVertexAttribArray(position); glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glVertexAttribPointer(position, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);
	glDrawArrays(GL_TRIANGLES, 0, 6); glDisableVertexAttribArray(position);
}

void Renderer::AddShader(GLuint program, const char* text, GLenum type)
{
	GLuint shader = glCreateShader(type); if (shader == 0) return;
	glShaderSource(shader, 1, &text, NULL); glCompileShader(shader);
	GLint success = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) { GLchar log[1024] = { 0 }; glGetShaderInfoLog(shader, sizeof(log), NULL, log); fprintf(stderr, "Shader error: %s\n", log); }
	glAttachShader(program, shader); glDeleteShader(shader);
}

bool Renderer::ReadFile(const char* filename, std::string* target)
{
	std::ifstream file(filename);
	if (!file) {
		// Resolve copied runtime shaders next to the executable as well as the VS working folder.
		wchar_t executable[MAX_PATH] = {};
		GetModuleFileNameW(NULL, executable, MAX_PATH);
		std::wstring path(executable);
		size_t slash=path.find_last_of(L"\\/");
		if (slash!=std::wstring::npos) {
			path.resize(slash+1);
			for (const char* c=filename; *c; ++c) path+=wchar_t(*c);
			file.clear();
			file.open(path.c_str());
		}
	}
	if (file.fail()) { std::cout << filename << " file loading failed..\n"; return false; }
	std::string line; while (getline(file, line)) *target += line + "\n";
	return true;
}

GLuint Renderer::CompileShaders(const char* filenameVS, const char* filenameFS)
{
	GLuint program = glCreateProgram(); if (program == 0) return 0;
	std::string vs, fs;
	if (!ReadFile(filenameVS, &vs) || !ReadFile(filenameFS, &fs)) { glDeleteProgram(program); return 0; }
	AddShader(program, vs.c_str(), GL_VERTEX_SHADER); AddShader(program, fs.c_str(), GL_FRAGMENT_SHADER);
	glLinkProgram(program); GLint success = 0; glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success) { GLchar log[1024] = { 0 }; glGetProgramInfoLog(program, sizeof(log), NULL, log); std::cout << "Shader link error: " << log << "\n"; glDeleteProgram(program); return 0; }
	return program;
}


void Renderer::DrawEllipse(float x,float y,float w,float h,float r,float g,float b,float a)
{
	int previous = m_Material;
	m_Material = 7;
	DrawSolidRect(x,y,w,h,r,g,b,a);
	m_Material = previous;
}

void Renderer::EndWorld(float time)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0,0,m_OutputWidth,m_OutputHeight);
	glClearColor(.025f,.035f,.04f,1);
	glClear(GL_COLOR_BUFFER_BIT);
	float sx=float(m_OutputWidth)/m_WindowSizeX, sy=float(m_OutputHeight)/m_WindowSizeY;
	float scale=sx<sy?sx:sy;
	int width=int(m_WindowSizeX*scale), height=int(m_WindowSizeY*scale);
	glViewport((m_OutputWidth-width)/2,(m_OutputHeight-height)/2,width,height);
	glDisable(GL_BLEND);
	glUseProgram(m_PostShader);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D,m_SceneTexture);
	glUniform1i(glGetUniformLocation(m_PostShader,"u_Scene"),0);
	glUniform2f(glGetUniformLocation(m_PostShader,"u_Pixel"),1.0f/m_WindowSizeX,1.0f/m_WindowSizeY);
	glUniform1f(glGetUniformLocation(m_PostShader,"u_Time"),time);
	GLint p = glGetAttribLocation(m_PostShader,"a_Position");
	glBindBuffer(GL_ARRAY_BUFFER,m_VBORect);
	glEnableVertexAttribArray(p);
	glVertexAttribPointer(p,3,GL_FLOAT,GL_FALSE,3*sizeof(float),0);
	glDrawArrays(GL_TRIANGLES,0,6);
	glDisableVertexAttribArray(p);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	m_Material = 0;
}

void Renderer::DrawQuad(float x0,float y0,float x1,float y1,float x2,float y2,float x3,float y3,float r,float g,float b)
{
	float left=x0, right=x0, top=y0, bottom=y0;
	const float xs[]={x0,x1,x2,x3}, ys[]={y0,y1,y2,y3};
	for(int i=1;i<4;++i) {
		if(xs[i]<left)left=xs[i]; if(xs[i]>right)right=xs[i];
		if(ys[i]<top)top=ys[i]; if(ys[i]>bottom)bottom=ys[i];
	}
	float w=right-left,h=bottom-top;
	if(w<=0 || h<=0)return;
	float cx=(left+right)*.5f,cy=(top+bottom)*.5f;
	float vertices[18];
	const int indices[]={0,1,2,0,2,3};
	for(int i=0;i<6;++i) {
		vertices[i*3]=(xs[indices[i]]-cx)/w;
		vertices[i*3+1]=(cy-ys[indices[i]])/h;
		vertices[i*3+2]=0;
	}
	glBindBuffer(GL_ARRAY_BUFFER,m_QuadBuffer);
	glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
	DrawShape(m_QuadBuffer,cx,cy,w,h,r,g,b,1);
}
