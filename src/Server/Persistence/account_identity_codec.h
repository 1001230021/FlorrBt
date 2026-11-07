#pragma once
#include <string>
#include <vector>
class CJsonValue;
struct SPlayerAccount;
std::string AccountIdentityJson(const SPlayerAccount&);
bool ReadAccountIdentity(const CJsonValue*, SPlayerAccount&, std::string* error);
bool ValidateAccountIdentities(std::vector<SPlayerAccount>&, bool& migrated, std::string* error);
