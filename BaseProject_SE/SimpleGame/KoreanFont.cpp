#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "KoreanFont.h"
#include "Profiler.h"
#include <iostream>

namespace
{
    HFONT CreateGameFont(int height, const wchar_t* family)
    {
        return CreateFontW(-height,
                           0,
                           0,
                           0,
                           FW_NORMAL,
                           FALSE,
                           FALSE,
                           FALSE,
                           HANGUL_CHARSET,
                           OUT_TT_PRECIS,
                           CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY,
                           FF_DONTCARE,
                           family);
    }

    bool SupportsHangul(HDC context, HFONT font)
    {
        if (!font)
        {
            return false;
        }
        HGDIOBJ previous = SelectObject(context, font);
        WORD glyphs[2]{};
        DWORD result = GetGlyphIndicesW(context, L"\ud55c\uae00", 2, glyphs, GGI_MARK_NONEXISTING_GLYPHS);
        SelectObject(context, previous);
        return result != GDI_ERROR && glyphs[0] != 0xffff && glyphs[1] != 0xffff && glyphs[0] && glyphs[1];
    }
} // namespace

KoreanFont::~KoreanFont()
{
    Shutdown();
}

bool KoreanFont::Initialize(int pixelHeight)
{
    Perf::Scope scope(Perf::Cpu::FontInit);
    Shutdown();
    // Font rasterization needs a GDI memory DC, not ownership of GLFW's OpenGL window DC.
    HDC context = CreateCompatibleDC(nullptr);
    if (!context)
    {
        return false;
    }
    m_Context = context;
    HFONT font = CreateGameFont(pixelHeight, L"Malgun Gothic");
    if (!SupportsHangul(context, font))
    {
        if (font)
        {
            DeleteObject(font);
        }
        wchar_t executable[32768]{};
        DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768)
        {
            Shutdown();
            return false;
        }
        m_PrivateFont.assign(executable, length);
        m_PrivateFont.resize(m_PrivateFont.find_last_of(L"\\/") + 1);
        m_PrivateFont += L"Fonts/NanumGothic-Regular.ttf";
        if (!AddFontResourceExW(m_PrivateFont.c_str(), FR_PRIVATE, nullptr))
        {
            m_PrivateFont.clear();
            Shutdown();
            return false;
        }
        font = CreateGameFont(pixelHeight, L"NanumGothic");
        std::cerr << "[font] bundled_fallback=NanumGothic; pixel_height=" << pixelHeight << '\n';
    }
    m_Font = font;
    if (!SupportsHangul(context, font))
    {
        Shutdown();
        return false;
    }
    return true;
}

void KoreanFont::Shutdown()
{
    if (m_Font)
    {
        DeleteObject(static_cast<HFONT>(m_Font));
        m_Font = nullptr;
    }
    if (m_Context)
    {
        DeleteDC(static_cast<HDC>(m_Context));
        m_Context = nullptr;
    }
    if (!m_PrivateFont.empty())
    {
        RemoveFontResourceExW(m_PrivateFont.c_str(), FR_PRIVATE, nullptr);
        m_PrivateFont.clear();
    }
}
