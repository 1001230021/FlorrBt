#pragma once

class CMobBase;
class CEntity;

class IController
{
  public:
    virtual ~IController() = default;
    virtual void OnTick(CMobBase* mob, float dt) = 0;
    virtual void OnDamaged(CMobBase*, CEntity*) {}
};
