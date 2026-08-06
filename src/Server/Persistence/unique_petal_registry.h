#pragma once

#include "../../Shared/petal_type.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct SUniquePetalOwner
{
    EPetalType type = EPetalType::None;
    int cost = 0;
    std::string owner;
};

class IUniquePetalRegistry
{
  public:
    virtual ~IUniquePetalRegistry() = default;

    virtual bool Init(const std::filesystem::path& path, std::string& error) = 0;
    virtual bool ShutDown(std::string& error) = 0;

    virtual const std::vector<SUniquePetalOwner>& GetOwners() const = 0;
    virtual const SUniquePetalOwner* FindOwner(EPetalType type) const = 0;
    virtual bool GetUnique(EPetalType type, int cost, const std::string& getter) = 0;
    virtual bool SetOwner(EPetalType type, int cost, std::string owner) = 0;
    virtual bool RemoveOwner(EPetalType type) = 0;
};

std::unique_ptr<IUniquePetalRegistry> CreateUniquePetalRegistry();
