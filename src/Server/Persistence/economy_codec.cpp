#include "economy_codec.h"
#include "../../Engine/json_value.h"
#include "../../Shared/petal_type.h"
#include "../../Shared/rarity.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <iomanip>
#include <sstream>

namespace
{
bool ReadCounter(const CJsonValue* value, std::uint64_t& result)
{
    if (!value || !value->IsString()) return false;
    const auto& text = value->AsString();
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
bool ReadNumber(const CJsonValue* value, std::uint64_t maximum, std::uint64_t& result)
{
    if (!value || !value->IsNumber() || !std::isfinite(value->AsNumber()) || value->AsNumber() < 0 ||
        value->AsNumber() > static_cast<double>(maximum) || std::floor(value->AsNumber()) != value->AsNumber()) return false;
    result = static_cast<std::uint64_t>(value->AsNumber()); return true;
}
}

bool ReadEconomyState(const CJsonValue* value, SAccountEconomyState& destination, std::string* error)
{
    SAccountEconomyState state;
    if (!value) { destination = {}; return true; } // 旧账号没有已处理请求记录。
    auto fail = [&](const char* message) { if (error) *error = message; return false; };
    if (!value->IsObject()) return fail("Invalid account economy state");
    const auto* version = value->Find("version"), *receipts = value->Find("receipts");
    if (!version || version->AsNumber(-1) != 1 || !receipts || !receipts->IsArray() ||
        receipts->AsArray().size() > SAccountEconomyState::receipt_limit ||
        !ReadCounter(value->Find("revision"), state.inventory_revision) ||
        !ReadCounter(value->Find("last_request_id"), state.last_request_id)) return fail("Invalid economy version/counters");
    for (const auto& entry : receipts->AsArray())
    {
        SEconomyReceipt receipt;
        std::uint64_t operation = 0, outcome = 0;
        const auto* fingerprint = entry.Find("fingerprint");
        if (!ReadCounter(entry.Find("request_id"), receipt.request_id) || !receipt.request_id ||
            receipt.request_id > state.last_request_id || (!state.receipts.empty() && receipt.request_id <= state.receipts.back().request_id) ||
            !ReadCounter(entry.Find("expected_revision"), receipt.expected_revision) ||
            !ReadCounter(entry.Find("result_revision"), receipt.result_revision) || receipt.result_revision > state.inventory_revision ||
            !ReadNumber(entry.Find("operation"), 3, operation) || operation < 1 ||
            !ReadNumber(entry.Find("outcome"), static_cast<unsigned>(EEconomyOutcome::WrongScope), outcome) ||
            !fingerprint || !fingerprint->IsString() || fingerprint->AsString().empty() || fingerprint->AsString().size() > 2048 ||
            fingerprint->AsString().size() % 2 != 0 || !std::all_of(fingerprint->AsString().begin(), fingerprint->AsString().end(),
                [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) return fail("Invalid economy receipt");
        receipt.operation = static_cast<EEconomyOperation>(operation);
        receipt.outcome = static_cast<EEconomyOutcome>(outcome);
        receipt.fingerprint = fingerprint->AsString();
        const auto* craft = entry.Find("craft");
        if (receipt.operation != EEconomyOperation::Loadout && !craft) return fail("Missing economy craft receipt");
        if (craft)
        {
            const auto* changed = craft->Find("changed"), *items = craft->Find("items");
            std::uint64_t type = 0, rarity = 0, consumed = 0, attempts = 0, successes = 0, result_rarity = 0;
            if (!changed || !changed->IsBool() || !items || !items->IsArray() || items->AsArray().size() > 16 ||
                !ReadNumber(craft->Find("type"), 255, type) || !ReadNumber(craft->Find("rarity"), 255, rarity) ||
                !ReadNumber(craft->Find("result_rarity"), 255, result_rarity) ||
                !ReadNumber(craft->Find("consumed"), UINT32_MAX, consumed) ||
                !ReadNumber(craft->Find("attempts"), UINT32_MAX, attempts) ||
                !ReadNumber(craft->Find("successes"), UINT32_MAX, successes)) return fail("Invalid economy craft receipt");
            receipt.craft = {changed->AsBool(), static_cast<uint8_t>(type), static_cast<uint8_t>(rarity),
                static_cast<uint32_t>(consumed), static_cast<uint32_t>(attempts), static_cast<uint32_t>(successes),
                static_cast<uint8_t>(result_rarity), {}};
            for (const auto& item : items->AsArray())
            {
                std::uint64_t count = 0;
                if (!item.IsArray() || item.AsArray().size() != 3 || !ReadNumber(&item.AsArray()[0], 255, type) ||
                    !ReadNumber(&item.AsArray()[1], 255, rarity) || !ReadNumber(&item.AsArray()[2], max_inventory_item_count, count) ||
                    !IsKnownPetalTypeId(static_cast<int>(type)) || !IsKnownRarity(static_cast<ERarity>(rarity)))
                    return fail("Invalid economy result item");
                receipt.craft.items.push_back({static_cast<uint8_t>(type), static_cast<uint8_t>(rarity), static_cast<uint32_t>(count)});
            }
        }
        state.receipts.push_back(std::move(receipt));
    }
    destination = std::move(state);
    return true;
}

void WriteEconomyState(std::ostream& output, const SAccountEconomyState& state)
{
    output << "{\"version\":1,\"revision\":\"" << state.inventory_revision << "\",\"last_request_id\":\""
           << state.last_request_id << "\",\"receipts\":[";
    bool first = true;
    for (const auto& receipt : state.receipts)
    {
        if (!first) output << ','; first = false;
        output << "{\"request_id\":\"" << receipt.request_id << "\",\"expected_revision\":\"" << receipt.expected_revision
               << "\",\"result_revision\":\"" << receipt.result_revision << "\",\"operation\":" << static_cast<unsigned>(receipt.operation)
               << ",\"outcome\":" << static_cast<unsigned>(receipt.outcome) << ",\"fingerprint\":" << std::quoted(receipt.fingerprint);
        {
            const auto& craft = receipt.craft;
            output << ",\"craft\":{\"changed\":" << (craft.changed ? "true" : "false") << ",\"type\":" << unsigned(craft.petal_type)
                   << ",\"rarity\":" << unsigned(craft.rarity) << ",\"result_rarity\":" << unsigned(craft.result_rarity)
                   << ",\"consumed\":" << craft.consumed << ",\"attempts\":" << craft.attempts
                   << ",\"successes\":" << craft.successes << ",\"items\":[";
            for (size_t i = 0; i < craft.items.size(); ++i)
            {
                if (i) output << ',';
                const auto& item = craft.items[i];
                output << '[' << unsigned(item.petal_type) << ',' << unsigned(item.rarity) << ',' << item.count << ']';
            }
            output << "]}";
        }
        output << '}';
    }
    output << "]}";
}

std::string EconomyStateJson(const SAccountEconomyState& state)
{
    std::ostringstream output; WriteEconomyState(output, state); return output.str();
}
