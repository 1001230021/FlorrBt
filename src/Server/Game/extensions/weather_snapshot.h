#pragma once
#include "../../HotReload/snapshot_archive.h"
class CWeatherExtension;

// JSON persistence adapter is separate from the dependency-free weather
// simulation, so standalone simulation tests do not need Jansson or SFML.
CJsonOwner CaptureWeatherSnapshot(const CWeatherExtension&);
bool RestoreWeatherSnapshot(CWeatherExtension&, const json_t*, std::uint32_t version, std::string& error);
