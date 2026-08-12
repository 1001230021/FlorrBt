#include "weather_diurnal_open_controller.h"
#include "../gamecontext.h"
#include "../gameworld.h"

void CWeatherDiurnalOpenController::OnActivate(CGameWorld& world)
{
    RefreshWeatherDiurnalSnapshot(world);
    COpenController::OnActivate(world);
}

void CWeatherDiurnalOpenController::OnTick(CGameWorld& world, float dt)
{
    RefreshWeatherDiurnalSnapshot(world);
    OnWeatherDiurnalTick(world, m_weather_diurnal, dt);
    COpenController::OnTick(world, dt);
}

void CWeatherDiurnalOpenController::RefreshWeatherDiurnalSnapshot(CGameWorld& world)
{
    CGameContext* context = world.GameContext();
    if (context) m_weather_diurnal = context->WeatherDiurnal().GetWeatherDiurnalSnapshot();
}
