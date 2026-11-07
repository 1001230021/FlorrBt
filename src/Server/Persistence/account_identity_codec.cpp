#include "account_identity_codec.h"
#include "account_store.h"
#include "../../Engine/json_value.h"
#include "../../Shared/tools.h"
#include "../../Shared/utf8.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
#include <sstream>
#include <tuple>

namespace
{
std::string Quote(std::string_view value)
{
    std::string out = "\"";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += char(ch); }
        else if (ch < 32) { constexpr char hex[] = "0123456789abcdef"; out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += char(ch);
    }
    return out + '"';
}
bool Hex(std::string_view value, size_t size)
{ return value.size() == size && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }); }
}
std::string AccountIdentityJson(const SPlayerAccount& account)
{
    std::ostringstream out; out << "{\"id\":" << Quote(account.account_id) << ",\"links\":[";
    bool comma = false;
    for (const auto& identity : account.linked_identities) {
        if (comma) out << ','; comma = true;
        out << "{\"provider\":" << Quote(identity.provider) << ",\"application\":" << Quote(identity.application)
            << ",\"subject\":" << Quote(identity.subject) << ",\"label\":" << Quote(identity.label) << ",\"linked_at\":" << identity.linked_at << '}';
    }
    return out.str() + "]}";
}
bool ReadAccountIdentity(const CJsonValue* value, SPlayerAccount& account, std::string* error)
{
    if (!value) return true;
    const auto* id = value->Find("id"), *links = value->Find("links");
    if (!value->IsObject() || !id || !id->IsString() || !links || !links->IsArray() || links->AsArray().size() > 2)
    { if (error) *error = "Invalid account identity state"; return false; }
    account.account_id = id->AsString();
    for (const auto& entry : links->AsArray()) {
        const auto* provider = entry.Find("provider"), *application = entry.Find("application"), *subject = entry.Find("subject"), *label = entry.Find("label"), *time = entry.Find("linked_at");
        if (!entry.IsObject() || !provider || !provider->IsString() || !application || !application->IsString() ||
            !subject || !subject->IsString() || !label || !label->IsString() || !time || !time->IsNumber() ||
            !std::isfinite(time->AsNumber()) || time->AsNumber() < 1 || time->AsNumber() > 9007199254740991.0 || std::floor(time->AsNumber()) != time->AsNumber())
        { if (error) *error = "Invalid external identity"; return false; }
        account.linked_identities.push_back({provider->AsString(), application->AsString(), subject->AsString(), label->AsString(), static_cast<std::int64_t>(time->AsNumber())});
    }
    return true;
}
bool ValidateAccountIdentities(std::vector<SPlayerAccount>& accounts, bool& migrated, std::string* error)
{
    std::set<std::string> ids;
    std::set<std::tuple<std::string, std::string, std::string>> subjects;
    for (auto& account : accounts) {
        if (account.name.starts_with("@sandbox:")) {
            if (!account.account_id.empty() || !account.linked_identities.empty()) { if (error) *error = "Temporary inventories cannot own login identities"; return false; }
            continue;
        }
        if (account.account_id.empty()) { if (!GenerateOpaqueId(account.account_id)) return false; migrated = true; }
        if (!Hex(account.account_id, 32) || !ids.insert(account.account_id).second || account.linked_identities.size() > 2)
        { if (error) *error = "Invalid or duplicate account id"; return false; }
        std::set<std::string> providers;
        for (const auto& identity : account.linked_identities) {
            const bool valid_subject = identity.provider == "discord" ? (!identity.subject.empty() && identity.subject.size() <= 32 &&
                std::all_of(identity.subject.begin(), identity.subject.end(), [](char c) { return c >= '0' && c <= '9'; })) :
                identity.provider == "qq" && identity.subject.size() == 32 && std::all_of(identity.subject.begin(), identity.subject.end(), [](unsigned char c) { return std::isxdigit(c); });
            if (!valid_subject || identity.application.empty() || identity.application.size() > 128 || identity.label.size() > 128 ||
                !IsValidUtf8(identity.label) || identity.linked_at <= 0 || !providers.insert(identity.provider).second ||
                !subjects.emplace(identity.provider, identity.application, identity.subject).second)
            { if (error) *error = "Conflicting or invalid external identity"; return false; }
        }
    }
    return true;
}
