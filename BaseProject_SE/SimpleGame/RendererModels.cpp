#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "Renderer.h"
#include "Profiler.h"
#include "CharacterPresentation.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    struct ComSession
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        ~ComSession()
        {
            if (SUCCEEDED(result))
            {
                CoUninitialize();
            }
        }
    };

    bool ReadBytes(const std::wstring& path, size_t limit, std::vector<unsigned char>& bytes)
    {
        std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
        if (!file)
        {
            return false;
        }
        auto size = file.tellg();
        if (size <= 0 || size > std::streamoff(limit))
        {
            return false;
        }
        bytes.resize(size_t(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
        Perf::Count(Perf::Counter::ResourceBytesRead, bytes.size());
        return bool(file);
    }

    std::uint32_t Crc32(const std::vector<unsigned char>& bytes)
    {
        std::uint32_t crc = 0xffffffffu;
        for (unsigned char value : bytes)
        {
            crc ^= value;
            for (int bit = 0; bit < 8; ++bit)
            {
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
            }
        }
        return crc ^ 0xffffffffu;
    }
} // namespace

bool Renderer::LoadResourceSprites(const std::wstring& directory)
{
    Perf::Scope scope(Perf::Cpu::ResourceLoad);
    std::vector<unsigned char> metadata, png;
    if (!ReadBytes(directory + L"/world_sprites.bin", 4 * 1024 * 1024, metadata) ||
        !ReadBytes(directory + L"/world_atlas.png", 16 * 1024 * 1024, png) || metadata.size() < 28 ||
        std::memcmp(metadata.data(), "HDSPRITE", 8) != 0)
    {
        return false;
    }
    size_t cursor = 8;
    auto read32 = [&]()
    {
        std::uint32_t value = 0;
        std::memcpy(&value, metadata.data() + cursor, 4);
        cursor += 4;
        return value;
    };
    const auto revision = read32(), width = read32(), height = read32(), count = read32(), crc = read32();
    if (revision != 2 || !width || !height || width > 8192 || height > 8192 ||
        std::uint64_t(width) * height > 16 * 1024 * 1024 || !count || count > 8192 || crc != Crc32(png))
    {
        return false;
    }
    std::map<std::string, ResourceSprite> sprites;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (cursor + 4 > metadata.size())
        {
            return false;
        }
        const auto length = read32();
        if (!length || length > 128 || cursor + length + 52 > metadata.size())
        {
            return false;
        }
        std::string name(reinterpret_cast<const char*>(metadata.data() + cursor), length);
        cursor += length;
        ResourceSprite sprite;
        std::memcpy(sprite.rect.data(), metadata.data() + cursor, 32);
        cursor += 32;
        sprite.material = read32();
        std::memcpy(sprite.weapon.data(), metadata.data() + cursor, 16);
        cursor += 16;
        for (float value : sprite.rect)
        {
            if (!std::isfinite(value) || std::abs(value) > 1024)
            {
                return false;
            }
        }
        for (float value : sprite.weapon)
        {
            if (!std::isfinite(value) || std::abs(value) > 1024)
            {
                return false;
            }
        }
        const auto& r = sprite.rect;
        if (r[2] <= 0 || r[3] <= 0 || r[4] < 0 || r[5] < 0 || r[6] <= 0 || r[7] <= 0 ||
            r[4] + r[6] > 1.00001f || r[5] + r[7] > 1.00001f || sprite.material < 22 ||
            sprite.material > 27 || !sprites.emplace(name, sprite).second)
        {
            return false;
        }
    }
    if (cursor != metadata.size())
    {
        return false;
    }

    ComSession session;
    if (FAILED(session.result) && session.result != RPC_E_CHANGED_MODE)
    {
        return false;
    }
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    UINT decodedWidth = 0, decodedHeight = 0;
    if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromMemory(png.data(), DWORD(png.size()))) ||
        FAILED(factory->CreateDecoderFromStream(
            stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())) ||
        FAILED(frame->GetSize(&decodedWidth, &decodedHeight)) || decodedWidth != width ||
        decodedHeight != height || FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(),
                                     GUID_WICPixelFormat32bppRGBA,
                                     WICBitmapDitherTypeNone,
                                     nullptr,
                                     0,
                                     WICBitmapPaletteTypeCustom)))
    {
        return false;
    }
    std::vector<unsigned char> pixels(size_t(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data())))
    {
        return false;
    }
    GLint maximum = 0, previousTexture = 0, alignment = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    if (width > std::uint32_t(maximum) || height > std::uint32_t(maximum))
    {
        return false;
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 GLsizei(width),
                 GLsizei(height),
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixels.data());
    GLint uploadedWidth = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &uploadedWidth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    glBindTexture(GL_TEXTURE_2D, GLuint(previousTexture));
    if (uploadedWidth != GLint(width))
    {
        glDeleteTextures(1, &texture);
        return false;
    }
    Flush();
    glDeleteTextures(1, &m_ResourceAtlas);
    m_ResourceAtlas = texture;
    m_ResourceSprites = std::move(sprites);
    Perf::Count(Perf::Counter::UploadCalls);
    Perf::Count(Perf::Counter::UploadBytes, pixels.size());
    Perf::Count(Perf::Counter::ResourceAtlasBytes, pixels.size());
    Perf::Count(Perf::Counter::ResourceSpritesLoaded, count);
    return true;
}

const Renderer::ResourceSprite* Renderer::FindResourceSprite(const std::string& name, int direction) const
{
    auto found = m_ResourceSprites.find(name);
    if (found == m_ResourceSprites.end())
    {
        found = m_ResourceSprites.find(
            name + "@" +
            std::to_string(std::max(0, std::min(CharacterPresentation::DirectionCount - 1, direction))));
    }
    return found == m_ResourceSprites.end() ? nullptr : &found->second;
}

bool Renderer::HasModel(const std::string& name) const
{
    return FindResourceSprite(name, 1) != nullptr;
}

float Renderer::ModelVisualHeight(const std::string& name) const
{
    float height = 0;
    for (int direction = 0; direction < CharacterPresentation::DirectionCount; ++direction)
    {
        if (const auto* sprite = FindResourceSprite(name, direction))
        {
            height = std::max(height, -sprite->rect[1]);
        }
    }
    return height;
}

void Renderer::DrawModel(const std::string& name,
                         float x,
                         float y,
                         bool mirrorX,
                         int direction,
                         float hitFlash,
                         const std::string& nextModel,
                         float poseBlend)
{
    // Imported direction-specific poses replace mirroring; keep the Actor flag compatible.
    (void)mirrorX;
    const auto* sprite = FindResourceSprite(name, direction);
    if (!sprite)
    {
        Perf::Count(Perf::Counter::ResourceLookupMiss);
        return;
    }
    Perf::Count(Perf::Counter::ResourceLookupHit);
    const bool character = name.compare(0, 4, "hero") == 0 || name.compare(0, 7, "citizen") == 0;
    if (character)
    {
        Perf::Count(Perf::Counter::CharacterInstances);
    }
    const auto& r = sprite->rect;
    const float widthScale = character || name == "corpse" ? CharacterPresentation::HumanWidthScale : 1.0f;
    const float color[] = {1, 1, 1, 1};
    const bool terrain = sprite->material >= 23 && sprite->material <= 25;
    const auto surface = m_SurfaceData;
    if (sprite->material == 22)
    {
        // x is reserved for pose mixing on sprites, w for the existing hit flash.
        m_SurfaceData[0] = 0;
        m_SurfaceData[3] = hitFlash;
    }
    const bool animated = character || name.compare(0, 5, "beast") == 0;
    if (animated && !nextModel.empty() && poseBlend > 0)
    {
        Perf::Scope scope(Perf::Cpu::PoseBlend);
        const auto* next = FindResourceSprite(nextModel, direction);
        if (next && next->material == 22 && sprite->material == 22)
        {
            const auto& n = next->rect;
            // A common foot-anchored canvas prevents cropped frames from sliding or clipping the blade.
            const float left = (std::min)(r[0], n[0]), top = (std::min)(r[1], n[1]);
            const float width = (std::max)(r[0] + r[2], n[0] + n[2]) - left;
            const float height = (std::max)(r[1] + r[3], n[1] + n[3]) - top;
            Instance item{};
            const float corners[] = {x + left * widthScale,
                                     y + top,
                                     x + left * widthScale,
                                     y + top + height,
                                     x + (left + width) * widthScale,
                                     y + top + height,
                                     x + (left + width) * widthScale,
                                     y + top};
            std::copy(corners, corners + 8, item.corners);
            std::copy(color, color + 4, item.color);
            std::copy(r.data() + 4, r.data() + 8, item.uv);
            std::copy(n.data() + 4, n.data() + 8, item.nextUV);
            const float maps[] = {(left - r[0]) / r[2], (top - r[1]) / r[3], width / r[2], height / r[3]};
            const float nextMaps[] = {(left - n[0]) / n[2], (top - n[1]) / n[3], width / n[2], height / n[3]};
            std::copy(maps, maps + 4, item.poseMap);
            std::copy(nextMaps, nextMaps + 4, item.nextPoseMap);
            item.material = 22;
            m_SurfaceData[0] = CharacterPresentation::Clamp01(poseBlend);
            QueueInstance(item, m_ResourceAtlas);
            m_SurfaceData = surface;
            Perf::Count(Perf::Counter::PoseBlendedInstances);
            Perf::Count(Perf::Counter::ResourceLookupHit);
            return;
        }
        Perf::Count(Perf::Counter::PoseBlendFallbacks);
    }
    // Scale local offsets with the width so the Actor's world foot anchor never drifts.
    QueueRect(x + (r[0] + r[2] * .5f) * widthScale,
              y + r[1] + r[3] * .5f,
              r[2] * widthScale + (terrain ? 1.0f : 0),
              r[3] + (terrain ? .5f : 0),
              color,
              int(sprite->material),
              m_ResourceAtlas,
              r.data() + 4);
    m_SurfaceData = surface;
}

void Renderer::DrawModelPreview(
    const std::string& name, float x, float y, float width, float height, int direction)
{
    const ResourceSprite* sprite = FindResourceSprite(name, direction);
    if (!sprite || width <= 0 || height <= 0)
    {
        Perf::Count(Perf::Counter::ResourceLookupMiss);
        return;
    }
    const auto& r = sprite->rect;
    const bool character = name.compare(0, 4, "hero") == 0;
    const float aspect = character ? CharacterPresentation::HumanWidthScale : 1.0f;
    const float fit = (std::min)(width / (r[2] * aspect), height / r[3]);
    const float color[] = {1, 1, 1, 1};
    const auto surface = m_SurfaceData;
    m_SurfaceData = {{0, 0, -1, 0}};
    QueueRect(x, y, r[2] * aspect * fit, r[3] * fit, color, 22, m_ResourceAtlas, r.data() + 4);
    m_SurfaceData = surface;
    Perf::Count(Perf::Counter::ResourceLookupHit);
    Perf::Count(Perf::Counter::InventoryPreviews);
}

void Renderer::DrawResourceSwordTrail(float frame, int direction, float x, float y, float strength)
{
    if (frame <= 0 || strength <= .01f)
    {
        return;
    }
    auto weaponAt = [&](float position, std::array<float, 4>& weapon)
    {
        const auto sample = CharacterPresentation::SamplePose(
            position / (CharacterPresentation::AttackFrames - 1), CharacterPresentation::AttackFrames, false);
        auto* current = FindResourceSprite("hero_attack" + std::to_string(sample.current), direction);
        auto* next = FindResourceSprite("hero_attack" + std::to_string(sample.next), direction);
        if (!current || !next)
        {
            return false;
        }
        for (size_t i = 0; i < weapon.size(); ++i)
        {
            weapon[i] = current->weapon[i] + (next->weapon[i] - current->weapon[i]) * sample.blend;
        }
        return true;
    };
    std::array<float, 4> a{}, b{};
    if (!weaponAt(frame, a) || !weaponAt((std::max)(0.0f, frame - 1), b))
    {
        return;
    }
    const float widthScale = CharacterPresentation::HumanWidthScale;
    DrawQuad(x + a[0] * widthScale,
             y + a[1],
             x + a[2] * widthScale,
             y + a[3],
             x + b[2] * widthScale,
             y + b[3],
             x + b[0] * widthScale,
             y + b[1],
             .84f,
             .91f,
             .89f,
             strength * .28f);
}
