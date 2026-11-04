#pragma once

#include <string>

class KoreanFont
{
  public:
    KoreanFont() = default;
    ~KoreanFont();
    KoreanFont(const KoreanFont&) = delete;
    KoreanFont& operator=(const KoreanFont&) = delete;
    bool Initialize(int pixelHeight);
    void Shutdown();

    void* Context() const
    {
        return m_Context;
    }

    void* Handle() const
    {
        return m_Font;
    }

  private:
    void* m_Context = nullptr;
    void* m_Font = nullptr;
    std::wstring m_PrivateFont;
};
