#include "API/ARK/Ark.h"

// Global state namespace (config, lastPreset, HTTP request helper)
#include "CousinCustomRates.h"

// Core logic: ReadConfig, ApplyRates, Discord webhook (rich embed), broadcast, state I/O
// Must be included before Hooks.h, Timers.h, and Commands.h (they call functions defined here)
#include "Utils.h"

// Tribe-only harvest boosts and the verified harvest-resource hook.
#include "TargetedRates.h"

// AShooterGameMode::InitGame hook — loads and queues the last preset on restart
// NOTE: SetHooks() is called in Plugin_Init() so the hook is registered BEFORE
//       InitGame fires. InitGame always runs before BeginPlay, so registering
//       the hook from OnServerReady() (BeginPlay) would be too late.
#include "Hooks.h"

// Scheduled automatic rate switching (timer-based, optional via config.json)
// Must be included after Utils.h (TimerCallback calls ApplyRates defined there)
#include "Timers.h"

// 'changerates <preset>' RCON command
#include "Commands.h"

// 'CousinCustomRates.Reload' RCON command
#include "Reload.h"

#pragma comment(lib, "AsaApi.lib")


// ---------------------------------------------------------------------------
// OnServerReady
//   Called once the game world is fully up (triggered by the BeginPlay hook,
//   or immediately if the plugin is loaded after the server is already ready).
//
//   By the time this runs, BeginPlay has restored persistent rates if a saved
//   preset existed in status.json.
//   This function registers the RCON commands, starts the optional schedule
//   timer, and does a fresh config reload (picks up any edits made between
//   restarts).
// ---------------------------------------------------------------------------
void OnServerReady()
{
	Log::GetLog()->info("CousinCustomRates: server ready — initialising plugin.");

	// Reload config — it was already read inside the InitGame hook, but reading
	// it again here picks up any edits the admin made since last restart and
	// ensures the commands/scheduler have the latest version.
	// Wrapped in try/catch: if config.json is malformed or missing at this
	// point the exception is logged and we continue — commands and the timer
	// will still register using whatever config was loaded during InitGame.
	try
	{
		ReadConfig();
	}
	catch (const std::exception& error)
	{
		Log::GetLog()->error(
			"OnServerReady: failed to reload config. ERROR: {} "
			"(using config loaded during InitGame hook).",
			error.what()
		);
	}

	AddOrRemoveCommands();
	AddReloadCommands();
	AddOrRemovePlayerRateCommands();
	AddOrRemoveTribeHarvestBoostTimer();
	SetTimers(); // starts the schedule timer only if Schedule.Enabled == true

	Log::GetLog()->info("CousinCustomRates initialised successfully.");
}


// ---------------------------------------------------------------------------
// BeginPlay hook
//   Ensures OnServerReady() runs after the game world has started.
//   BeginPlay fires after InitGame, so this is where a preset queued by the
//   InitGame hook can be restored once GameState replication is available.
// ---------------------------------------------------------------------------
DECLARE_HOOK(AShooterGameMode_BeginPlay, void, AShooterGameMode*);

void Hook_AShooterGameMode_BeginPlay(AShooterGameMode* _this)
{
	AShooterGameMode_BeginPlay_original(_this);
	RestoreQueuedStartupPreset(_this);
	OnServerReady();
}


// ---------------------------------------------------------------------------
// Plugin entry points
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) void Plugin_Init()
{
	Log::Get().Init(PROJECT_NAME);

	// Register the InitGame hook FIRST and EARLY — InitGame fires before
	// BeginPlay, so this hook must be set before the game reaches that point.
	// Failing to do this here would mean the hook misses the first InitGame
	// call and rates would NOT be restored after a restart.
	SetHooks();
	SetTribeHarvestBoostHooks();

	// Register the BeginPlay hook to trigger our post-world-ready setup.
	AsaApi::GetHooks().SetHook(
		"AShooterGameMode.BeginPlay()",
		Hook_AShooterGameMode_BeginPlay,
		&AShooterGameMode_BeginPlay_original
	);

	// Hot-reload / late-load safety: if the server is already running when
	// this plugin loads, BeginPlay will never fire again, so call OnServerReady
	// directly.
	if (AsaApi::GetApiUtils().GetStatus() == AsaApi::ServerStatus::Ready)
		OnServerReady();
}

extern "C" __declspec(dllexport) void Plugin_Unload()
{
	AsaApi::GetHooks().DisableHook(
		"AShooterGameMode.BeginPlay()",
		Hook_AShooterGameMode_BeginPlay
	);

	// Remove the InitGame hook
	SetHooks(false);
	SetTribeHarvestBoostHooks(false);

	// Stop the schedule timer (safe even if it was never started)
	SetTimers(false);
	AddOrRemoveTribeHarvestBoostTimer(false);

	// Remove RCON commands
	AddOrRemoveCommands(false);
	AddReloadCommands(false);
	AddOrRemovePlayerRateCommands(false);

	Log::GetLog()->info("CousinCustomRates unloaded.");
}
