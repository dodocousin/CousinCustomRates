#pragma once

#include <ctime>        // std::time, std::tm, localtime_s
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Schedule Timer
//
// PURPOSE: Automatically switch rate presets based on the current day and
//          hour, without any admin RCON input.
//
// HOW IT WORKS:
//   The ASA API timer fires every 1 second. Each tick increments a counter.
//   When the counter reaches "CheckIntervalSeconds" from config.json, the
//   schedule is evaluated against the current local server time, then the
//   counter resets.
//
// RULE MATCHING:
//   Rules are evaluated in ORDER. The first rule whose Day list contains the
//   current day AND whose [StartHour, EndHour] range (inclusive) contains the
//   current hour (0-23) is applied. If the matching preset is already active,
//   nothing happens (no spam). If no rule matches, the current preset is left
//   unchanged.
//
// TIMEZONE:
//   Uses the SERVER machine's local timezone (std::localtime / localtime_s).
//   Make sure your server OS clock is set to your intended timezone.
//
// OVERNIGHT RULES:
//   StartHour must be <= EndHour. Overnight ranges (e.g. 22-02) are NOT
//   supported. Use two rules (22-23 and 00-02) as a workaround.
//
// CONFIG STRUCTURE:
//   "Schedule": {
//     "Enabled": true,
//     "CheckIntervalSeconds": 60,
//     "Rules": [
//       {
//         "Preset": "weekend_rates",
//         "Days":   ["Friday", "Saturday", "Sunday"],
//         "StartHour": 18,
//         "EndHour":   23
//       },
//       {
//         "Preset": "normal_rates",
//         "Days":   ["Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"],
//         "StartHour": 0,
//         "EndHour":   17
//       }
//     ]
//   }
// ---------------------------------------------------------------------------

// Seconds elapsed since the last schedule evaluation
static int s_scheduleTickCounter = 0;

// Maps English day names to tm_wday values (0=Sunday … 6=Saturday)
static const std::unordered_map<std::string, int> s_dayNameMap = {
	{"Sunday",    0},
	{"Monday",    1},
	{"Tuesday",   2},
	{"Wednesday", 3},
	{"Thursday",  4},
	{"Friday",    5},
	{"Saturday",  6}
};

// ---------------------------------------------------------------------------
// CheckSchedule
//   Called by TimerCallback() when the interval has elapsed.
//   Delegates all rule evaluation to GetCurrentSchedulePreset() (Utils.h)
//   which supports both the new day-range format and the legacy Days-array
//   format, as well as per-rule Enabled toggles and timed-preset blocking.
//
//   Exits early if:
//     • A timed preset countdown is active (timedPresetExpiry > 0)
//     • GetCurrentSchedulePreset() returns an empty string (no rule matches)
//     • The matched preset is already the active one (no-op, no Discord spam)
// ---------------------------------------------------------------------------
void CheckSchedule()
{
	// Block schedule switching while a timed preset countdown is running.
	if (CousinCustomRates::timedPresetExpiry > 0) return;

	const std::string targetPreset = GetCurrentSchedulePreset();
	if (targetPreset.empty()) return;
	if (targetPreset == CousinCustomRates::lastPreset) return;

	Log::GetLog()->info(
		"Schedule: switching from '{}' to '{}'.",
		CousinCustomRates::lastPreset, targetPreset
	);

	ApplyRates(FString(targetPreset.c_str()));
}

// ---------------------------------------------------------------------------
// TimerCallback
//   Fires every 1 second (ASA API default timer interval).
//   Accumulates ticks and only evaluates the schedule once per
//   CheckIntervalSeconds to avoid unnecessary overhead.
// ---------------------------------------------------------------------------
void TimerCallback()
{
	if (!CousinCustomRates::config.contains("Schedule")) return;
	if (!CousinCustomRates::config["Schedule"].value("Enabled", false)) return;

	// Clamp to a minimum of 1 second — a zero or negative value would make
	// the schedule fire on every single tick, causing constant file I/O and
	// HTTP requests which would hurt server performance.
	// Note: std::max is avoided here because Windows headers define a 'max'
	// macro that conflicts with it. A ternary is used instead.
	const int rawInterval = CousinCustomRates::config["Schedule"].value("CheckIntervalSeconds", 60);
	const int interval    = (rawInterval < 1) ? 1 : rawInterval;

	s_scheduleTickCounter++;
	if (s_scheduleTickCounter < interval) return;

	s_scheduleTickCounter = 0;
	CheckSchedule();
}

// ---------------------------------------------------------------------------
// SetTimers
//   Registers or removes the per-second timer callback.
//   Called from OnServerReady() (addTmr=true) and Plugin_Unload() (addTmr=false).
//   If the schedule is disabled in config, the timer is not registered and
//   there is zero runtime overhead.
// ---------------------------------------------------------------------------
void SetTimers(bool addTmr = true)
{
	// Only operate the timer when the schedule feature is actually enabled
	const bool scheduleEnabled =
		CousinCustomRates::config.contains("Schedule") &&
		CousinCustomRates::config["Schedule"].value("Enabled", false);

	if (addTmr)
	{
		if (!scheduleEnabled) return;

		s_scheduleTickCounter = 0; // reset on (re-)register
		AsaApi::GetCommands().AddOnTimerCallback("CousinCustomRatesTimer", &TimerCallback);
		Log::GetLog()->info("Schedule timer registered (interval: {}s).",
			CousinCustomRates::config["Schedule"].value("CheckIntervalSeconds", 60));
	}
	else
	{
		// Always attempt removal — safe even if it was never registered
		AsaApi::GetCommands().RemoveOnTimerCallback("CousinCustomRatesTimer");
	}
}
