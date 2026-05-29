#pragma once

// GameMode.h provides the AShooterGameMode multiplier field accessors
// (TamingSpeedMultiplierField, XPMultiplierField, etc.)
#include <API/ARK/Ark.h>
#include <API/ARK/GameMode.h>

// ---------------------------------------------------------------------------
// AShooterGameMode::InitGame hook
//
// PURPOSE: Re-apply the last-active rate preset on every server (re)start,
//          making the rate change persistent across restarts.
//
// TIMING:  InitGame fires BEFORE BeginPlay. Because of this, the hook must
//          be registered inside Plugin_Init() — not in OnServerReady() —
//          to guarantee it is active when InitGame fires during startup.
//
// SAFETY:  We always call the original function first so the server
//          completes its own initialization before we touch any fields.
//          We also call ReadConfig() here because config.json may not have
//          been loaded yet (OnServerReady / BeginPlay comes after InitGame).
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

	Log::GetLog()->info("InitGame hook: re-applying saved preset '{}'.", savedPreset);

	// 4. Apply the multipliers into the freshly initialised GameMode (_this).
	//    Pass sendNotifications=false so Discord and in-game broadcasts are
	//    suppressed — this is a silent restore on restart, not a rate change.
	if (!ApplyRates(FString(savedPreset.c_str()), false))
	{
		Log::GetLog()->error("InitGame hook: failed to apply saved preset '{}'.", savedPreset);
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
