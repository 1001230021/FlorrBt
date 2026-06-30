#pragma once

class IModule{
  public:
    virtual ~IModule() = default;
    virtual bool Init() = 0;
    virtual void Tick(float dt) = 0;
    virtual void ShutDown() = 0;
};