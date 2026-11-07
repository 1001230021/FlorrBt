#pragma once

#include "account_store.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class IUniquePetalRegistry
{
  public:
    virtual ~IUniquePetalRegistry() = default;

    virtual bool Init(const std::filesystem::path& path, std::string& error) = 0;
    virtual bool ShutDown(std::string& error) = 0;

    virtual const std::vector<SUniquePetalOwner>& GetOwners() const = 0;
    virtual const SUniquePetalOwner* FindOwner(EPetalType type) const = 0;
    virtual bool ExchangeUnique(EPetalType type, int cost, const std::string& getter,
                                SUniquePetalExchangeResult& result, std::string& error) = 0;
};

std::unique_ptr<IUniquePetalRegistry> CreateUniquePetalRegistry();
