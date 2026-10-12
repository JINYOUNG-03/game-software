#pragma once

#include <string>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <array>
#include <map>
#include <vector>
#include <functional>
#include <cstdint>

class Actor;
class SceneGraph;
enum class ScenePass;

#include "Dependencies\glew.h"

class Renderer
{
  public:
    Renderer(int windowSizeX, int windowSizeY);
    ~Renderer();

    bool IsInitialized();
    void BeginFrame(float r, float g, float b, float a);
    void EndWorld(float time);
    void EndFrame();
    void Flush();
    void DrawText(float x, float y, const wchar_t* text, float r, float g, float b, void* dc, void* font);
    float TextWidth(const wchar_t* text, void* dc, void* font);
    float TextAdvance(wchar_t character, void* dc, void* font);
    void DrawMiniMapLayer(std::uint64_t revision, float cx, float cy, const std::function<void()>& draw);
    void FinishOverlay();

    void SetOutputSize(int w, int h)
    {
        m_OutputWidth = w;
        m_OutputHeight = h;
    }

    void DrawQuad(float x0,
                  float y0,
                  float x1,
                  float y1,
                  float x2,
                  float y2,
                  float x3,
                  float y3,
                  float r,
                  float g,
                  float b);

    void SetTime(float time)
    {
        m_Time = time;
    }

    void SetMaterial(int material)
    {
        m_Material = material;
    }

    void DrawEllipse(float x, float y, float w, float h, float r, float g, float b, float a);
    void DrawSolidRect(float x, float y, float width, float height, float r, float g, float b, float a);
    void DrawSolidDiamond(float x, float y, float width, float height, float r, float g, float b, float a);

    bool LoadModels(const std::wstring& path);
    bool SaveModels(const std::wstring& path) const;
    bool HasModel(const std::string& name) const;
    void BeginModel(const std::string& name);
    void EndModel();
    void DrawModel(const std::string& name, float x, float y);
    void DrawScene(const SceneGraph& scene, ScenePass pass, const std::function<void(const Actor&)>& draw);

    void SetScreenOffset(float x, float y)
    {
        m_OffsetX = x;
        m_OffsetY = y;
    }

  private:
    void Initialize(int windowSizeX, int windowSizeY);
    bool ReadFile(const char* filename, std::string* target);
    void AddShader(GLuint ShaderProgram, const char* pShaderText, GLenum ShaderType);
    GLuint CompileShaders(const char* filenameVS, const char* filenameFS);
    void CreateVertexBufferObjects();
    void SubmitDrawArrays(GLenum mode, GLint first, GLsizei count);
    void DrawShape(
        GLuint vertexBuffer, float x, float y, float width, float height, float r, float g, float b, float a);

    struct Instance
    {
        float corners[8]{};
        float color[4]{};
        float uv[4]{0, 0, 1, 1};
        float material = 0;
    };

    static_assert(sizeof(Instance) == sizeof(float) * 17, "Instance data must not contain padding.");

    struct GpuBatch
    {
        GLuint buffer = 0;
        GLuint vao = 0;
        std::vector<Instance> instances;
    };

    struct Glyph
    {
        GLuint texture = 0;
        float u = 0, v = 0, uw = 0, vh = 0;
        int width = 0, height = 0, bearingX = 0, bearingY = 0, advance = 9;
    };

    struct ProgramLocations
    {
        GLint screen = -1, time = -1, texture = -1;
    } m_BatchLocations;

    GLint m_PostScene = -1, m_PostGlow = -1, m_PostPixel = -1, m_PostMode = -1;
    GLuint m_PostVAO = 0;
    std::array<GpuBatch, 64> m_GpuBatches;
    size_t m_BatchSlot = 0;
    std::vector<Instance> m_Queue;
    GLuint m_QueueTexture = 0;
    float m_TargetWidth = 1280, m_TargetHeight = 800;
    std::array<GLuint, 2> m_GlowFbo{}, m_GlowTexture{};
    GLuint m_MiniMapFbo = 0, m_MiniMapTexture = 0;
    std::uint64_t m_MiniMapRevision = 0;
    bool m_MiniMapValid = false;
    std::vector<GLuint> m_FontPages;
    std::map<wchar_t, Glyph> m_Glyphs;
    int m_FontX = 1, m_FontY = 1, m_FontRow = 0;
    std::map<std::string, std::vector<Instance>> m_CompiledModels;
    bool m_CompileModel = false;
    std::vector<Instance> m_ModelInstances;
    void QueueInstance(Instance instance, GLuint texture = 0);
    void QueueRect(float x,
                   float y,
                   float w,
                   float h,
                   const float* color,
                   int material,
                   GLuint texture = 0,
                   const float* uv = nullptr);
    bool CreateTarget(GLuint& fbo, GLuint& texture, int width, int height);
    void DrawPost(GLuint target, GLuint source, GLuint glow, int mode, int width, int height);
    Glyph CreateGlyph(wchar_t c, void* dc, void* font);
    bool m_Initialized = false;

    unsigned int m_WindowSizeX = 0;
    unsigned int m_WindowSizeY = 0;

    GLuint m_VBORect = 0;
    GLuint m_VBODiamond = 0;
    GLuint m_SolidRectShader = 0;
    GLuint m_Framebuffer = 0, m_SceneTexture = 0, m_PostShader = 0;
    int m_Material = 0;
    float m_Time = 0;
    int m_OutputWidth = 1280, m_OutputHeight = 800;
    GLuint m_QuadBuffer = 0;

    struct ModelPart
    {
        int shape = 0;
        int material = 0;
        std::array<float, 12> values = {};
    };

    std::map<std::string, std::vector<ModelPart>> m_Models;
    std::string m_RecordingModel;
    float m_OffsetX = 0;
    float m_OffsetY = 0;
    std::uint64_t m_FrameNumber = 0;
    std::uint64_t m_DrawArraysCalls = 0;
};
