#pragma once

class CMobBase;

class IController {
public:
    virtual ~IController() = default;
    virtual void OnTick(CMobBase* mob, float dt) = 0;
};