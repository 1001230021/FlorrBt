#pragma once

#include <memory>
#include <string>

class IUniquePetalRegistry;

class IServerDataStore
{
  public:
    virtual ~IServerDataStore() = default;

    virtual bool Init(std::string& error) = 0;
    virtual bool ShutDown(std::string& error) = 0;
    virtual IUniquePetalRegistry& UniquePetals() = 0;
    virtual const IUniquePetalRegistry& UniquePetals() const = 0;
};

std::unique_ptr<IServerDataStore> CreateServerDataStore();
