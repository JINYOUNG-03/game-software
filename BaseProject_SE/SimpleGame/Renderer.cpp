#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "Renderer.h"
#include "SceneGraph.h"
#include "Profiler.h"
#include <cmath>
#include <algorithm>
#include <iomanip>

Renderer::Renderer(int windowSizeX, int windowSizeY)
{
    Initialize(windowSizeX, windowSizeY);
}

Renderer::~Renderer()
{
    for (auto& batch : m_GpuBatches)
    {
        if (batch.vao)
        {
            glDeleteVertexArrays(1, &batch.vao);
        }
        if (batch.buffer)
        {
            glDeleteBuffers(1, &batch.buffer);
        }
    }
    glDeleteTextures(1, &m_ResourceAtlas);
    glDeleteVertexArrays(1, &m_PostVAO);
    glDeleteFramebuffers(2, m_GlowFbo.data());
    glDeleteTextures(2, m_GlowTexture.data());
    glDeleteFramebuffers(1, &m_MiniMapFbo);
    glDeleteTextures(1, &m_MiniMapTexture);
    if (!m_FontPages.empty())
    {
        glDeleteTextures(GLsizei(m_FontPages.size()), m_FontPages.data());
    }
    if (m_QuadBuffer)
    {
        glDeleteBuffers(1, &m_QuadBuffer);
    }
    if (m_Framebuffer)
    {
        glDeleteFramebuffers(1, &m_Framebuffer);
    }
    if (m_SceneTexture)
    {
        glDeleteTextures(1, &m_SceneTexture);
    }
    if (m_PostShader)
    {
        glDeleteProgram(m_PostShader);
    }
    if (m_VBORect != 0)
    {
        glDeleteBuffers(1, &m_VBORect);
    }
    if (m_VBODiamond != 0)
    {
        glDeleteBuffers(1, &m_VBODiamond);
    }
    if (m_SolidRectShader != 0)
    {
        glDeleteProgram(m_SolidRectShader);
    }
}

void Renderer::Initialize(int windowSizeX, int windowSizeY)
{
    m_WindowSizeX = windowSizeX;
    m_WindowSizeY = windowSizeY;
    m_OutputWidth = windowSizeX;
    m_OutputHeight = windowSizeY;

    m_SolidRectShader = CompileShaders("./Shaders/SolidRect.vs", "./Shaders/SolidRect.fs");
    m_PostShader = CompileShaders("./Shaders/Post.vs", "./Shaders/Post.fs");

    bool targetReady = CreateTarget(m_Framebuffer, m_SceneTexture, windowSizeX, windowSizeY);
    for (int i = 0; i < 2; ++i)
    {
        targetReady =
            CreateTarget(m_GlowFbo[i], m_GlowTexture[i], windowSizeX / 4, windowSizeY / 4) && targetReady;
    }
    targetReady = CreateTarget(m_MiniMapFbo, m_MiniMapTexture, 240, 225) && targetReady;
    m_BatchLocations.screen = glGetUniformLocation(m_SolidRectShader, "u_Screen");
    m_BatchLocations.time = glGetUniformLocation(m_SolidRectShader, "u_Time");
    m_BatchLocations.texture = glGetUniformLocation(m_SolidRectShader, "u_Texture");
    m_BatchLocations.worldView = glGetUniformLocation(m_SolidRectShader, "u_WorldView");
    m_BatchLocations.worldShading = glGetUniformLocation(m_SolidRectShader, "u_WorldShading");
    m_BatchLocations.lights = glGetUniformLocation(m_SolidRectShader, "u_Lights[0]");
    m_BatchLocations.lightCount = glGetUniformLocation(m_SolidRectShader, "u_LightCount");
    m_PostScene = glGetUniformLocation(m_PostShader, "u_Scene");
    m_PostGlow = glGetUniformLocation(m_PostShader, "u_Glow");
    m_PostPixel = glGetUniformLocation(m_PostShader, "u_Pixel");
    m_PostMode = glGetUniformLocation(m_PostShader, "u_Mode");
    m_PostTime = glGetUniformLocation(m_PostShader, "u_Time");
    CreateVertexBufferObjects();
    glGenVertexArrays(1, &m_PostVAO);
    glBindVertexArray(m_PostVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBORect);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
    glBindVertexArray(0);
    m_Queue.reserve(8192);
    m_Initialized = targetReady && m_PostShader && m_SolidRectShader && m_PostVAO;
}

bool Renderer::IsInitialized()
{
    return m_Initialized;
}

void Renderer::BeginFrame(float r, float g, float b, float a)
{
    ++m_FrameNumber;
    m_DrawArraysCalls = 0;
    m_BatchSlot = 0;
    m_QueueTexture = 0;
    m_TargetWidth = float(m_WindowSizeX);
    m_TargetHeight = float(m_WindowSizeY);
    m_WorldShading = true;
    m_SurfaceData = {{0, 0, -1, 0}};

    SetScreenOffset(0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
    glViewport(0, 0, m_WindowSizeX, m_WindowSizeY);

    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::EndFrame()
{
    Perf::Profiler::Get().EndFrame();
}

void Renderer::SubmitDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    glDrawArrays(mode, first, count);
    ++m_DrawArraysCalls;
    Perf::Count(Perf::Counter::DrawCalls);
}

void Renderer::DrawScene(const SceneGraph& scene,
                         ScenePass pass,
                         const std::function<void(const Actor&)>& draw)
{
    // Painting does not mutate the graph; its list remains valid for the entire pass.
    for (const Actor* actor : scene.FrameList(pass))
    {
        draw(*actor);
    }
}

void Renderer::CreateVertexBufferObjects()
{
    glGenBuffers(1, &m_QuadBuffer);
    const float rect[] = {
        -0.5f, -0.5f, 0, -0.5f, 0.5f, 0, 0.5f, 0.5f, 0, -0.5f, -0.5f, 0, 0.5f, 0.5f, 0, 0.5f, -0.5f, 0};
    const float diamond[] = {0, -0.5f, 0, -0.5f, 0, 0, 0, 0.5f, 0, 0, -0.5f, 0, 0, 0.5f, 0, 0.5f, 0, 0};
    glGenBuffers(1, &m_VBORect);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBORect);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rect), rect, GL_STATIC_DRAW);
    glGenBuffers(1, &m_VBODiamond);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBODiamond);
    glBufferData(GL_ARRAY_BUFFER, sizeof(diamond), diamond, GL_STATIC_DRAW);
}

void Renderer::DrawSolidRect(float x, float y, float width, float height, float r, float g, float b, float a)
{
    DrawShape(m_VBORect, x, y, width, height, r, g, b, a);
}

void Renderer::DrawSolidDiamond(
    float x, float y, float width, float height, float r, float g, float b, float a)
{
    DrawShape(m_VBODiamond, x, y, width, height, r, g, b, a);
}

void Renderer::DrawShape(
    GLuint vertexBuffer, float x, float y, float width, float height, float r, float g, float b, float a)
{
    if (!m_Initialized)
    {
        return;
    }
    const float color[] = {r, g, b, a};
    if (vertexBuffer == m_VBODiamond)
    {
        Instance item{};
        const float corners[] = {
            x, y - height * .5f, x - width * .5f, y, x, y + height * .5f, x + width * .5f, y};
        std::copy(corners, corners + 8, item.corners);
        std::copy(color, color + 4, item.color);
        item.material = float(m_Material);
        QueueInstance(item);
    }
    else
    {
        QueueRect(x, y, width, height, color, m_Material);
    }
}

void Renderer::AddShader(GLuint program, const char* text, GLenum type)
{
    GLuint shader = glCreateShader(type);
    if (shader == 0)
    {
        return;
    }
    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        GLchar log[1024] = {0};
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "Shader error: %s\n", log);
    }
    glAttachShader(program, shader);
    glDeleteShader(shader);
}

bool Renderer::ReadFile(const char* filename, std::string* target)
{
    std::ifstream file(filename);
    if (!file)
    {
        // Resolve copied runtime shaders next to the executable as well as the VS working folder.
        wchar_t executable[MAX_PATH] = {};
        GetModuleFileNameW(NULL, executable, MAX_PATH);
        std::wstring path(executable);
        size_t slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            path.resize(slash + 1);
            for (const char* c = filename; *c; ++c)
            {
                path += wchar_t(*c);
            }
            file.clear();
            file.open(path.c_str());
        }
    }
    if (file.fail())
    {
        std::cout << filename << " file loading failed..\n";
        return false;
    }
    std::string line;
    while (getline(file, line))
    {
        *target += line + "\n";
    }
    return true;
}

GLuint Renderer::CompileShaders(const char* filenameVS, const char* filenameFS)
{
    GLuint program = glCreateProgram();
    if (program == 0)
    {
        return 0;
    }
    std::string vs, fs;
    if (!ReadFile(filenameVS, &vs) || !ReadFile(filenameFS, &fs))
    {
        glDeleteProgram(program);
        return 0;
    }
    AddShader(program, vs.c_str(), GL_VERTEX_SHADER);
    AddShader(program, fs.c_str(), GL_FRAGMENT_SHADER);
    glLinkProgram(program);
    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        GLchar log[1024] = {0};
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        std::cout << "Shader link error: " << log << "\n";
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void Renderer::DrawEllipse(float x, float y, float w, float h, float r, float g, float b, float a)
{
    int previous = m_Material;
    m_Material = 7;
    DrawSolidRect(x, y, w, h, r, g, b, a);
    m_Material = previous;
}

void Renderer::EndWorld(float time)
{
    Flush();
    m_WorldShading = false;
    m_SurfaceData = {{0, 0, -1, 0}};
    Perf::Scope scope(Perf::Cpu::Postprocess);
    glDisable(GL_BLEND);
    const int lowWidth = m_WindowSizeX / 4, lowHeight = m_WindowSizeY / 4;
    DrawPost(m_GlowFbo[0], m_SceneTexture, 0, 0, lowWidth, lowHeight);
    DrawPost(m_GlowFbo[1], m_GlowTexture[0], 0, 1, lowWidth, lowHeight);
    DrawPost(m_GlowFbo[0], m_GlowTexture[1], 0, 2, lowWidth, lowHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_OutputWidth, m_OutputHeight);
    glClearColor(.025f, .035f, .04f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    float scale = (std::min)(float(m_OutputWidth) / m_WindowSizeX, float(m_OutputHeight) / m_WindowSizeY);
    int width = int(m_WindowSizeX * scale), height = int(m_WindowSizeY * scale);
    glViewport((m_OutputWidth - width) / 2, (m_OutputHeight - height) / 2, width, height);
    DrawPost(0, m_SceneTexture, m_GlowTexture[0], 3, width, height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_Material = 0;
    Perf::Profiler::Get().GpuMark("postprocess");
}

void Renderer::DrawQuad(float x0,
                        float y0,
                        float x1,
                        float y1,
                        float x2,
                        float y2,
                        float x3,
                        float y3,
                        float r,
                        float g,
                        float b,
                        float a)
{
    Instance item{};
    const float corners[] = {x0, y0, x1, y1, x2, y2, x3, y3};
    const float color[] = {r, g, b, a};
    std::copy(corners, corners + 8, item.corners);
    std::copy(color, color + 4, item.color);
    item.material = float(m_Material);
    QueueInstance(item);
}
