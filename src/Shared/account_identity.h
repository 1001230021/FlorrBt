#pragma once
#include <cstdint>
#include <string>

struct SLinkedIdentity
{
    std::string provider, application, subject, label;
    std::int64_t linked_at = 0;
    bool operator==(const SLinkedIdentity&) const = default;
};
