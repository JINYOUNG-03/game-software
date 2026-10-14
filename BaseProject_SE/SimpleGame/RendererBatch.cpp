#include "stdafx.h"
#include <Windows.h>
#include "Renderer.h"
#include "Profiler.h"
#include <algorithm>
#include <cstddef>
#include <cstring>

bool Renderer::CreateTarget(GLuint& fbo, GLuint& texture, int width, int height)
{
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    bool ready = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ready;
}

void Renderer::QueueInstance(Instance instance, GLuint texture)
{
    if (m_CompileModel)
    {
        m_ModelInstances.push_back(instance);
        return;
    }
    std::copy(m_SurfaceData.begin(), m_SurfaceData.end(), instance.surface);
    if (texture && texture != m_QueueTexture)
    {
        Perf::Count(Perf::Counter::TextureFlush);
        Flush();
        m_QueueTexture = texture;
    }
    if (m_Queue.size() == 8192)
    {
        Perf::Count(Perf::Counter::BatchCapacityFlush);
        Flush();
    }
    for (int i = 0; i < 8; i += 2)
    {
        instance.corners[i] += m_OffsetX;
        instance.corners[i + 1] += m_OffsetY;
    }
    m_Queue.push_back(instance);
}

void Renderer::QueueRect(
    float x, float y, float w, float h, const float* color, int material, GLuint texture, const float* uv)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }
    Instance item{};
    const float corners[] = {x - w * .5f,
                             y - h * .5f,
                             x - w * .5f,
                             y + h * .5f,
                             x + w * .5f,
                             y + h * .5f,
                             x + w * .5f,
                             y - h * .5f};
    std::copy(corners, corners + 8, item.corners);
    std::copy(color, color + 4, item.color);
    if (uv)
    {
        std::copy(uv, uv + 4, item.uv);
    }
    item.material = float(material);
    QueueInstance(item, texture);
}

void Renderer::Flush()
{
    if (m_Queue.empty())
    {
        return;
    }
    Perf::Scope scope(Perf::Cpu::BatchSubmit);
    auto& slot = m_GpuBatches[m_BatchSlot++ % m_GpuBatches.size()];
    const bool fresh = slot.buffer == 0;
    if (fresh)
    {
        glGenBuffers(1, &slot.buffer);
        glGenVertexArrays(1, &slot.vao);
    }
    glBindVertexArray(slot.vao);
    glBindBuffer(GL_ARRAY_BUFFER, slot.buffer);
    const size_t bytes = m_Queue.size() * sizeof(Instance);
    // Unchanged static batches remain GPU resident. Changed batches orphan the previous storage.
    if (slot.instances.size() != m_Queue.size() || std::memcmp(slot.instances.data(), m_Queue.data(), bytes))
    {
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(bytes), m_Queue.data(), GL_STREAM_DRAW);
        slot.instances = m_Queue;
        Perf::Count(Perf::Counter::UploadCalls);
        Perf::Count(Perf::Counter::UploadBytes, bytes);
    }
    else
    {
        Perf::Count(Perf::Counter::BufferReuse);
    }
    if (fresh)
    {
        const size_t offsets[] = {offsetof(Instance, corners),
                                  offsetof(Instance, corners) + 4 * sizeof(float),
                                  offsetof(Instance, color),
                                  offsetof(Instance, uv),
                                  offsetof(Instance, material),
                                  offsetof(Instance, surface)};
        for (GLuint i = 0; i < 6; ++i)
        {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i,
                                  i == 4 ? 1 : 4,
                                  GL_FLOAT,
                                  GL_FALSE,
                                  sizeof(Instance),
                                  reinterpret_cast<const void*>(offsets[i]));
            glVertexAttribDivisor(i, 1);
        }
    }
    glUseProgram(m_SolidRectShader);
    glUniform2f(m_BatchLocations.screen, m_TargetWidth, m_TargetHeight);
    glUniform1f(m_BatchLocations.time, m_Time);
    glUniform1i(m_BatchLocations.texture, 0);
    glUniform4f(m_BatchLocations.worldView,
                m_CameraX,
                m_CameraY,
                float(m_WindowSizeX) * .5f,
                float(m_WindowSizeY) * .52f);
    glUniform1i(m_BatchLocations.worldShading, m_WorldShading ? 1 : 0);
    glUniform1i(m_BatchLocations.lightCount, m_LightCount);
    glUniform4fv(m_BatchLocations.lights, 8, m_Lights.data());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_QueueTexture);
    // Instance order is queue order: never regroup translucent primitives by material.
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(m_Queue.size()));
    ++m_DrawArraysCalls;
    Perf::Count(Perf::Counter::DrawCalls);
    Perf::Count(Perf::Counter::Instances, m_Queue.size());
    m_Queue.clear();
    glBindVertexArray(0);
}

void Renderer::DrawPost(GLuint target, GLuint source, GLuint glow, int mode, int width, int height)
{
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    if (target)
    {
        glViewport(0, 0, width, height);
    }
    glUseProgram(m_PostShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, glow);
    glUniform1i(m_PostScene, 0);
    glUniform1i(m_PostGlow, 1);
    glUniform1i(m_PostMode, mode);
    glUniform1f(m_PostTime, m_Time);
    glUniform2f(m_PostPixel, 4.0f / m_WindowSizeX, 4.0f / m_WindowSizeY);
    glBindVertexArray(m_PostVAO);
    SubmitDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::FinishOverlay()
{
    Flush();
    Perf::Profiler::Get().GpuMark("overlay");
}

void Renderer::DrawMiniMapLayer(std::uint64_t revision, float cx, float cy, const std::function<void()>& draw)
{
    if (!m_MiniMapValid || revision != m_MiniMapRevision)
    {
        Perf::Scope scope(Perf::Cpu::MiniMapRebuild);
        Perf::Count(Perf::Counter::MiniMapCacheMiss);
        Flush();
        m_QueueTexture = 0;
        GLint framebuffer = 0, viewport[4]{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        float offsetX = m_OffsetX, offsetY = m_OffsetY;
        m_OffsetX = 120 - cx;
        m_OffsetY = 112.5f - cy;
        m_TargetWidth = 240;
        m_TargetHeight = 225;
        glBindFramebuffer(GL_FRAMEBUFFER, m_MiniMapFbo);
        glViewport(0, 0, 240, 225);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        draw();
        Flush();
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(framebuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        m_OffsetX = offsetX;
        m_OffsetY = offsetY;
        m_TargetWidth = float(m_WindowSizeX);
        m_TargetHeight = float(m_WindowSizeY);
        m_MiniMapRevision = revision;
        m_MiniMapValid = true;
    }
    else
    {
        Perf::Count(Perf::Counter::MiniMapCacheHit);
    }
    const float color[] = {1, 1, 1, 1};
    QueueRect(cx, cy, 240, 225, color, 11, m_MiniMapTexture);
}

Renderer::Glyph Renderer::CreateGlyph(wchar_t c, void* dcHandle, void* fontHandle)
{
    Perf::Scope scope(Perf::Cpu::GlyphRasterize);
    Perf::Count(Perf::Counter::GlyphCacheMiss);
    Glyph glyph;
    HDC dc = static_cast<HDC>(dcHandle);
    HGDIOBJ previous = SelectObject(dc, static_cast<HFONT>(fontHandle));
    MAT2 transform{};
    transform.eM11.value = 1;
    transform.eM22.value = 1;
    GLYPHMETRICS metrics{};
    DWORD bytes = GetGlyphOutlineW(dc, c, GGO_GRAY8_BITMAP, &metrics, 0, nullptr, &transform);
    std::vector<unsigned char> bitmap;
    bool valid = bytes != GDI_ERROR;
    if (valid && bytes)
    {
        bitmap.resize(bytes);
        valid = GetGlyphOutlineW(dc, c, GGO_GRAY8_BITMAP, &metrics, bytes, bitmap.data(), &transform) !=
                GDI_ERROR;
    }
    SelectObject(dc, previous);
    if (!valid)
    {
        Perf::Count(Perf::Counter::GlyphFailure);
        return glyph;
    }
    glyph.advance = metrics.gmCellIncX;
    glyph.width = int(metrics.gmBlackBoxX);
    glyph.height = int(metrics.gmBlackBoxY);
    glyph.bearingX = metrics.gmptGlyphOrigin.x;
    glyph.bearingY = metrics.gmptGlyphOrigin.y;
    if (!glyph.width || !glyph.height || bitmap.empty())
    {
        return glyph;
    }
    constexpr int extent = 1024;
    if (glyph.width + 2 >= extent || glyph.height + 2 >= extent)
    {
        Perf::Count(Perf::Counter::GlyphFailure);
        glyph.width = 0;
        return glyph;
    }
    if (m_FontX + glyph.width + 1 >= extent)
    {
        m_FontX = 1;
        m_FontY += m_FontRow + 1;
        m_FontRow = 0;
    }
    if (m_FontPages.empty() || m_FontY + glyph.height + 1 >= extent)
    {
        if (m_FontPages.size() >= 16)
        {
            Perf::Count(Perf::Counter::GlyphFailure);
            glyph.width = 0;
            return glyph;
        }
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        std::vector<unsigned char> blank(extent * extent, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, extent, extent, 0, GL_RED, GL_UNSIGNED_BYTE, blank.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_FontPages.push_back(texture);
        m_FontX = m_FontY = 1;
        m_FontRow = 0;
        Perf::Count(Perf::Counter::UploadBytes, blank.size());
        Perf::Count(Perf::Counter::UploadCalls);
    }
    const int pitch = (glyph.width + 3) & ~3;
    std::vector<unsigned char> coverage(size_t(glyph.width) * glyph.height);
    for (int y = 0; y < glyph.height; ++y)
    {
        for (int x = 0; x < glyph.width; ++x)
        {
            coverage[size_t(y) * glyph.width + x] =
                static_cast<unsigned char>((std::min)(255, int(bitmap[size_t(y) * pitch + x]) * 255 / 64));
        }
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_FontPages.back());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    m_FontX,
                    m_FontY,
                    glyph.width,
                    glyph.height,
                    GL_RED,
                    GL_UNSIGNED_BYTE,
                    coverage.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    Perf::Count(Perf::Counter::UploadBytes, coverage.size());
    Perf::Count(Perf::Counter::UploadCalls);
    glyph.texture = m_FontPages.back();
    glyph.u = float(m_FontX) / extent;
    glyph.v = float(m_FontY) / extent;
    glyph.uw = float(glyph.width) / extent;
    glyph.vh = float(glyph.height) / extent;
    m_FontX += glyph.width + 1;
    m_FontRow = (std::max)(m_FontRow, glyph.height);
    return glyph;
}

float Renderer::TextAdvance(wchar_t character, void* dc, void* font)
{
    auto it = m_Glyphs.find(character);
    if (it == m_Glyphs.end())
    {
        it = m_Glyphs.emplace(character, CreateGlyph(character, dc, font)).first;
    }
    return static_cast<float>(it->second.advance);
}

float Renderer::TextWidth(const wchar_t* text, void* dc, void* font)
{
    Perf::Scope scope(Perf::Cpu::Text);
    float width = 0;
    for (const wchar_t* c = text; *c; ++c)
    {
        width += TextAdvance(*c, dc, font);
    }
    return width;
}

void Renderer::DrawText(
    float x, float y, const wchar_t* text, float r, float g, float b, void* dc, void* font)
{
    Perf::Scope scope(Perf::Cpu::Text);
    const float color[] = {r, g, b, 1};
    for (const wchar_t* c = text; *c; ++c)
    {
        auto it = m_Glyphs.find(*c);
        if (it == m_Glyphs.end())
        {
            it = m_Glyphs.emplace(*c, CreateGlyph(*c, dc, font)).first;
        }
        const Glyph& glyph = it->second;
        if (glyph.texture && glyph.width)
        {
            const float uv[] = {glyph.u, glyph.v, glyph.uw, glyph.vh};
            QueueRect(x + glyph.bearingX + glyph.width * .5f,
                      y - glyph.bearingY + glyph.height * .5f,
                      float(glyph.width),
                      float(glyph.height),
                      color,
                      10,
                      glyph.texture,
                      uv);
            Perf::Count(Perf::Counter::Glyphs);
        }
        x += glyph.advance;
    }
}
