#pragma once
#include "../../HotReload/snapshot_archive.h"
class CWeatherExtension;

// 持久化适配独立于天气模拟，避免模拟测试依赖 Jansson 或 SFML。
CJsonOwner CaptureWeatherSnapshot(const CWeatherExtension&);
bool RestoreWeatherSnapshot(CWeatherExtension&, const json_t*, std::uint32_t version, std::string& error);
