#include "test_framework.h"
#include "Shared/utf8.h"
#include <string>
#include <string_view>

namespace
{
bool ValidPrefix(std::string_view text, std::string_view truncated)
{
    return truncated.size() <= text.size() && text.substr(0, truncated.size()) == truncated;
}

bool EndsOnBoundary(std::string_view truncated)
{
    if (truncated.empty()) return true;
    const unsigned char last = static_cast<unsigned char>(truncated.back());
    // A valid truncation ends on an ASCII byte or the continuation byte that
    // closes a multi-byte sequence; a dangling lead byte (110xxxxx and up)
    // means we cut mid-character.
    return last < 0x80 || (last & 0xC0) == 0x80;
}
} // namespace

TEST(TruncateUtf8KeepsAsciiUntouched)
{
    CHECK(TruncateUtf8("hello", 32) == "hello");
    CHECK(TruncateUtf8("hello", 5) == "hello");
    CHECK(TruncateUtf8("hello", 3) == "hel");
    CHECK(TruncateUtf8("hello", 0).empty());
}

TEST(TruncateUtf8DoesNotSplitTwoByteCharacter)
{
    // 'e-acute' is C3 A9 (2 bytes). A cut of 1 byte must not leave a dangling lead byte.
    const std::string text = "a\xC3\xA9"; // "a" + e-acute
    const auto truncated = TruncateUtf8(text, 2);
    CHECK(truncated == "a");
    CHECK(EndsOnBoundary(truncated));
}

TEST(TruncateUtf8DoesNotSplitThreeByteCharacter)
{
    // 'CJK' is E4 B8 AD (3 bytes). Cuts of 2, 3, 4 must be clean.
    const std::string text = "\xE4\xB8\xAD"; // CJK char
    CHECK(TruncateUtf8(text, 2).empty());
    CHECK(TruncateUtf8(text, 3) == text);
    CHECK(TruncateUtf8(text, 4) == text);

    const std::string text2 = "ab\xE4\xB8\xAD" "cd";
    const auto cut = TruncateUtf8(text2, 4); // "ab" + 1 byte of the CJK char
    CHECK(cut == "ab");
    CHECK(EndsOnBoundary(cut));
}

TEST(TruncateUtf8DoesNotSplitFourByteCharacter)
{
    // U+1F600 emoji is F0 9F 98 80.
    const std::string text = "\xF0\x9F\x98\x80";
    CHECK(TruncateUtf8(text, 3).empty());
    CHECK(TruncateUtf8(text, 4) == text);
    CHECK(TruncateUtf8(text, 5) == text);
}

TEST(TruncateUtf8ExactBoundaryIsKept)
{
    const std::string text = "\xC3\xA9\xE4\xB8\xAD"; // e-acute + CJK
    const auto cut = TruncateUtf8(text, 2);          // cut right after e-acute
    CHECK(cut == "\xC3\xA9");
    CHECK(EndsOnBoundary(cut));
}

TEST(TruncateUtf8NeverGrowsInput)
{
    const std::string text = "\xF0\x9F\x98\x80\xE4\xB8\xAD\xC3\xA9" "x";
    for (size_t limit = 0; limit <= text.size(); ++limit)
    {
        const auto cut = TruncateUtf8(text, limit);
        CHECK(cut.size() <= limit);
        CHECK(ValidPrefix(text, cut));
        CHECK(EndsOnBoundary(cut));
    }
}

TEST(IsValidUtf8AcceptsValidText)
{
    CHECK(IsValidUtf8(""));
    CHECK(IsValidUtf8("hello"));
    CHECK(IsValidUtf8("a\xC3\xA9"));                                   // e-acute
    CHECK(IsValidUtf8("\xE4\xB8\xAD"));                                // CJK
    CHECK(IsValidUtf8("\xF0\x9F\x98\x80"));                            // emoji
    CHECK(IsValidUtf8("mix \xC3\xA9 \xE4\xB8\xAD \xF0\x9F\x98\x80 z")); // mixed
}

TEST(IsValidUtf8RejectsTruncatedSequences)
{
    CHECK(!IsValidUtf8("\xC3"));         // lead byte missing its continuation
    CHECK(!IsValidUtf8("\xE4\xB8"));     // 3-byte sequence missing last byte
    CHECK(!IsValidUtf8("\xF0\x9F\x98")); // 4-byte sequence missing last byte
    CHECK(!IsValidUtf8("ab\xE4\xB8"));   // truncated at the end of a string
}

TEST(IsValidUtf8RejectsStrayContinuationAndInvalidLeads)
{
    CHECK(!IsValidUtf8("\x80"));
    CHECK(!IsValidUtf8("\xBF"));
    CHECK(!IsValidUtf8("a\x80" "b"));
    CHECK(!IsValidUtf8("\xF8\x88\x80\x80\x80")); // 5-byte lead never valid
    CHECK(!IsValidUtf8("\xC0\xAF"));             // overlong 2-byte form of '/'
    CHECK(!IsValidUtf8("\xE0\x80\xAF"));         // overlong 3-byte form
    CHECK(!IsValidUtf8("\xF0\x80\x80\xAF"));     // overlong 4-byte form
}

TEST(IsValidUtf8RejectsSurrogatesAndOutOfRange)
{
    CHECK(!IsValidUtf8("\xED\xA0\x80"));     // U+D800 surrogate
    CHECK(!IsValidUtf8("\xED\xBF\xBF"));     // U+DFFF surrogate
    CHECK(!IsValidUtf8("\xF4\x90\x80\x80")); // U+110000 out of range
    CHECK(!IsValidUtf8("\xF5\x80\x80\x80")); // way out of range
}

TEST(IsValidUtf8ContinuationMustFollowLead)
{
    CHECK(!IsValidUtf8("\xC3" "A"));  // ASCII where continuation expected
    CHECK(!IsValidUtf8("\xE4" "AB")); // ASCII where continuations expected
}