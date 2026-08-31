#pragma once

// GameMode.h provides the AShooterGameMode multiplier field accessors
// (TamingSpeedMultiplierField, XPMultiplierField, etc.)
#include <API/ARK/Ark.h>
#include <API/ARK/GameMode.h>

// ---------------------------------------------------------------------------
// AShooterGameMode::InitGame hook
//
// PURPOSE: Load the last-active rate preset on every server (re)start and
//          queue it for restoration from BeginPlay.
//
// TIMING:  InitGame fires BEFORE BeginPlay. Because of this, the hook must
//          be registered inside Plugin_Init() — not in OnServerReady() —
//          to guarantee it is active when InitGame fires during startup.
//
// SAFETY:  AShooterGameState is not guaranteed to exist during InitGame, even
//          after the original function returns. We therefore load state here
//          but defer changing/replicating multipliers until BeginPlay.
// ---------------------------------------------------------------------------

DECLARE_HOOK(
	AShooterGameMode_InitGame,
	void,
	AShooterGameMode* _this,
	const FString& MapName,
	const FString& Options,
	FString& ErrorMessage
)
{
	// 1. Let the game mode finish its own standard initialization first
	AShooterGameMode_InitGame_original(_this, MapName, Options, ErrorMessage);

	// 2. Load config.json now — OnServerReady (BeginPlay) hasn't run yet at
	//    this point, so CousinCustomRates::config may still be empty.
	try
	{
		ReadConfig();
	}
	catch (const std::exception& error)
	{
		Log::GetLog()->error("InitGame hook: failed to load config. ERROR: {}", error.what());
		return;
	}

	// 3. Check if a preset was previously saved
	std::string savedPreset = LoadState();

	if (savedPreset.empty())
	{
		Log::GetLog()->info("InitGame hook: no saved preset found — using server defaults.");
		return;
	}

	CousinCustomRates::pendingStartupPreset = savedPreset;
	CousinCustomRates::startupRestorePending = true;
	Log::GetLog()->info(
		"InitGame hook: saved preset '{}' queued for BeginPlay restore.", savedPreset);
}

// ---------------------------------------------------------------------------
// RestoreQueuedStartupPreset
//   Runs after AShooterGameMode::BeginPlay. At this point the GameState field
//   is expected to be available, so replicated EggHatch changes can be safely
//   applied. Timed preset state is handled only after the initial restoration
//   has succeeded.
// ---------------------------------------------------------------------------
void RestoreQueuedStartupPreset(AShooterGameMode* gameMode)
{
	if (!CousinCustomRates::startupRestorePending)
		return;

	const std::string savedPreset = CousinCustomRates::pendingStartupPreset;
	if (savedPreset.empty())
	{
		CousinCustomRates::startupRestorePending = false;
		return;
	}

	Log::GetLog()->info("BeginPlay hook: restoring saved preset '{}'.", savedPreset);

	// Silent restore. fromTimedExpiry prevents ApplyRates from overwriting the
	// persisted timed state before the logic below resumes or expires it.
	if (!ApplyRates(FString(savedPreset.c_str()), false, true, gameMode))
	{
		Log::GetLog()->error("BeginPlay hook: failed to restore saved preset '{}'.", savedPreset);
		return;
	}

	CousinCustomRates::startupRestorePending = false;
	CousinCustomRates::pendingStartupPreset.clear();

	// LoadState() in InitGame populated timedPresetExpiry and timedFallbackPreset.
	// Decide whether to apply a downtime-expired fallback or re-arm the timer.
	if (CousinCustomRates::timedPresetExpiry > 0)
	{
		const int64_t now       = static_cast<int64_t>(std::time(nullptr));
		const int64_t remaining = CousinCustomRates::timedPresetExpiry - now;

		if (remaining <= 0)
		{
			// Timer expired while the server was down — apply the fallback now.
			Log::GetLog()->info(
				"BeginPlay hook: timed preset expired during downtime — applying fallback.");

			CousinCustomRates::timedPresetExpiry = 0;

			const std::string schedPreset = GetCurrentSchedulePreset();
			const std::string target =
				!schedPreset.empty() ? schedPreset : CousinCustomRates::timedFallbackPreset;

			CousinCustomRates::timedFallbackPreset.clear();

			if (!target.empty())
			{
				// sendNotifications=true  — real expiry, players should be notified
				// fromTimedExpiry=true    — don't re-arm a timer for the fallback
				ApplyRates(FString(target.c_str()), true, true, gameMode);
			}
			else
			{
				Log::GetLog()->warn(
					"BeginPlay hook: timed preset expired but no fallback found — "
					"rates unchanged.");
			}
		}
		else
		{
			// Timer still valid — re-arm with the remaining seconds.
			Log::GetLog()->info(
				"BeginPlay hook: timed preset still active, re-arming with {}s remaining.",
				remaining);

			ArmTimedPresetExpiry(remaining);
		}
	}
}

// ---------------------------------------------------------------------------
// SetHooks / UnsetHooks
//
// Called from Plugin_Init() to register the InitGame hook EARLY (before the
// game fires InitGame), and from Plugin_Unload() to cleanly remove it.
// ---------------------------------------------------------------------------
void SetHooks(bool addHooks = true)
{
	if (addHooks)
	{
		AsaApi::GetHooks().SetHook(
			"AShooterGameMode.InitGame(FString&,FString&,FString&)",
			&Hook_AShooterGameMode_InitGame,
			&AShooterGameMode_InitGame_original
		);
		Log::GetLog()->info("Hook 'AShooterGameMode::InitGame' registered.");
	}
	else
	{
		AsaApi::GetHooks().DisableHook(
			"AShooterGameMode.InitGame(FString&,FString&,FString&)",
			&Hook_AShooterGameMode_InitGame
		);
	}
}
