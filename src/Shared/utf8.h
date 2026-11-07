#pragma once

#include <cstddef>
#include <string_view>

// 按完整 UTF-8 码点截断到字节上限，避免拆开中文等多字节字符。
inline std::string_view TruncateUtf8(std::string_view text, size_t max_bytes)
{
    if (text.size() <= max_bytes) return text;
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    return text.substr(0, end);
}

// 严格拒绝无效首字节或续字节、过长编码、代理区码点及超过 U+10FFFF 的码点。
inline bool IsValidUtf8(std::string_view text)
{
    size_t i = 0;
    while (i < text.size())
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80)
        {
            ++i;
            continue;
        }

        size_t extra = 0;
        uint32_t code_point = 0;
        if ((c & 0xE0) == 0xC0)
        {
            extra = 1;
            code_point = c & 0x1F;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            extra = 2;
            code_point = c & 0x0F;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            extra = 3;
            code_point = c & 0x07;
        }
        else
        {
            return false;
        }
        if (i + extra >= text.size()) return false;

        for (size_t k = 1; k <= extra; ++k)
        {
            const unsigned char cont = static_cast<unsigned char>(text[i + k]);
            if ((cont & 0xC0) != 0x80) return false;
            code_point = (code_point << 6) | (cont & 0x3F);
        }
        if ((extra == 1 && code_point < 0x80) || (extra == 2 && code_point < 0x800) ||
            (extra == 3 && code_point < 0x10000) || (code_point >= 0xD800 && code_point <= 0xDFFF) ||
            code_point > 0x10FFFF)
            return false;
        i += extra + 1;
    }
    return true;
}
