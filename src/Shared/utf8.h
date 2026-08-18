#pragma once
// UTF-8 helpers that are deliberately free of SFML and other engine
// dependencies so they can be compiled and unit-tested standalone.

#include <cstddef>
#include <string_view>

// Truncates UTF-8 text to at most max_bytes bytes without splitting a
// multi-byte code point. Byte-level substring cuts (std::string::substr) can
// split e.g. a 3-byte CJK character in the middle, corrupting the message.
inline std::string_view TruncateUtf8(std::string_view text, size_t max_bytes)
{
    if (text.size() <= max_bytes) return text;
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    return text.substr(0, end);
}