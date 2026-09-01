#pragma once

#include <cstdint>      // int64_t (timed preset expiry timestamp)
#include <string>
#include <unordered_map>
#include "json.hpp"
#include "Requests.h"
#include "Timer.h"

namespace CousinCustomRates
{
	// A harvest-only override assigned to an ARK tribe/team. The multiplier is
	// interpreted as an absolute target rate; the harvest hook compensates for
	// whichever global HarvestAmountMultiplier is currently active.
	struct TribeHarvestBoost
	{
		std::string presetName;
		float harvestMultiplier = 1.0f;
		int64_t expiryUnixTime = 0; // 0 = permanent
		std::string notificationEosId;
	};

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

	// Targeted harvest boosts, keyed by ARK TargetingTeam / tribe ID.
	inline std::unordered_map<int, TribeHarvestBoost> tribeHarvestBoosts;

	// Last known total tribe membership count, keyed by the local ARK team ID.
	// This is refreshed from online player states and lets unmounted tames use
	// the tribe-size modifier after a player from their tribe has been seen.
	inline std::unordered_map<int, int> tribeMemberCountsByTeam;
}
