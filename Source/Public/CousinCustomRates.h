#pragma once

#include <cstdint>      // int64_t (timed preset expiry timestamp)
#include "json.hpp"
#include "Requests.h"
#include "Timer.h"

namespace CousinCustomRates
{
	// Parsed config.json contents
	inline nlohmann::json config;

	// Name of the currently active rate preset (e.g. "weekend_rates")
	inline std::string lastPreset;

	// Preset loaded during InitGame and deferred until BeginPlay, when the
	// AShooterGameState required for replication is available.
	inline std::string pendingStartupPreset;
	inline bool startupRestorePending = false;

	// HTTP request helper (used for Discord webhook)
	static API::Requests& req = API::Requests::Get();

	// ---------------------------------------------------------------------------
	// Timed Preset State
	//
	// timedPresetExpiry  — Unix timestamp (seconds) at which the active timed
	//                      preset should revert.  0 = no timed preset running.
	//
	// timedFallbackPreset — The preset to restore when the timer fires.
	//                       Captured automatically from lastPreset the first time
	//                       a timed preset is armed; NOT updated if a second timed
	//                       preset is applied while one is already running, so the
	//                       revert always goes back to the original pre-event state.
	// ---------------------------------------------------------------------------
	inline int64_t    timedPresetExpiry    = 0;
	inline std::string timedFallbackPreset;
}
