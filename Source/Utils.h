#pragma once

#include <fstream>
#include <string>
#include <ctime>          // std::time, std::tm, localtime_s / localtime_r
#include <unordered_map>  // GetCurrentSchedulePreset day map
#include <unordered_set>  // ValidateConfig duplicate tribe-size entries

// ---------------------------------------------------------------------------
// Forward declaration
//   ApplyRates is defined later in this file.  ArmTimedPresetExpiry (defined
//   before it) fires a lambda that calls ApplyRates, so we need this forward
//   declaration to allow the compiler to resolve the name at lambda compile time.
// ---------------------------------------------------------------------------
bool ApplyRates(const FString& presetName,
                bool sendNotifications = true,
                bool fromTimedExpiry   = false,
                AShooterGameMode* gameModeOverride = nullptr);

// ---------------------------------------------------------------------------
// ValidateConfig
//   Inspects the loaded config for structural problems and logs warnings /
//   errors for anything suspicious.  Never throws — the plugin keeps running
//   even when issues are found.  Call this immediately after parsing JSON so
//   admins see actionable feedback in the server log on every (re-)load.
//
//   Checks:
//     • RatePresets (required, must be object)
//     • Per preset: 5 multiplier fields present / positive numbers;
//                   Discord_Webhook starts with "https://";
//                   Duration (if present) is a positive integer
//     • TimedPresets block: Enabled is bool
//     • Schedule block: Enabled, CheckIntervalSeconds, Rules, per-rule fields,
//                       valid day names, hour range, StartHour <= EndHour,
//                       Preset references an existing rate preset
// ---------------------------------------------------------------------------
void ValidateConfig()
{
	const nlohmann::json& cfg = CousinCustomRates::config;
	int issueCount = 0;

	auto warn = [&](const std::string& msg)
	{
		Log::GetLog()->warn("ValidateConfig: {}", msg);
		++issueCount;
	};

	// ---- 1. Top-level structure ----------------------------------------
	if (!cfg.contains("RatePresets") || !cfg["RatePresets"].is_object())
	{
		Log::GetLog()->error("ValidateConfig: 'RatePresets' key is missing or not an object — "
			"no presets will be available.");
		return;
	}

	// ---- 2. Per-preset validation --------------------------------------
	const std::vector<std::string> multiplierFields = {
		"TamingSpeedMultiplier",
		"XPMultiplier",
		"HarvestAmountMultiplier",
		"BabyMatureSpeedMultiplier",
		"EggHatchSpeedMultiplier"
	};

	for (auto& [presetKey, preset] : cfg["RatePresets"].items())
	{
		if (!preset.is_object())
		{
			warn("Preset '" + presetKey + "' is not a JSON object — skipping.");
			continue;
		}

		for (const auto& field : multiplierFields)
		{
			if (!preset.contains(field))
				warn("Preset '" + presetKey + "': missing '" + field + "' — will default to 1.0.");
			else if (!preset[field].is_number())
				warn("Preset '" + presetKey + "': '" + field + "' is not a number — will default to 1.0.");
			else if (preset[field].get<float>() <= 0.0f)
				warn("Preset '" + presetKey + "': '" + field + "' is <= 0 — likely a configuration mistake.");
		}

		if (preset.contains("Discord_Webhook") && preset["Discord_Webhook"].is_string())
		{
			const std::string url = preset["Discord_Webhook"].get<std::string>();
			if (!url.empty() && url.rfind("https://", 0) != 0)
				warn("Preset '" + presetKey + "': 'Discord_Webhook' does not start with 'https://'.");
		}

		if (preset.contains("Duration"))
		{
			if (!preset["Duration"].is_number_integer())
				warn("Preset '" + presetKey + "': 'Duration' must be an integer (minutes).");
			else if (preset["Duration"].get<int64_t>() <= 0)
				warn("Preset '" + presetKey + "': 'Duration' is <= 0 — preset will not be timed.");
		}
	}

	// ---- 3. TimedPresets block -----------------------------------------
	if (cfg.contains("TimedPresets"))
	{
		const nlohmann::json& tp = cfg["TimedPresets"];
		if (!tp.is_object())
			warn("'TimedPresets' is not a JSON object.");
		else if (tp.contains("Enabled") && !tp["Enabled"].is_boolean())
			warn("TimedPresets.Enabled is not a boolean.");
	}

	// ---- 3b. Tribe-size harvest multipliers -----------------------------
	if (cfg.contains("TribeSizeMultiplier"))
	{
		const nlohmann::json& sizeSettings = cfg["TribeSizeMultiplier"];
		if (!sizeSettings.is_object())
		{
			warn("'TribeSizeMultiplier' is not a JSON object.");
		}
		else
		{
			if (sizeSettings.contains("Enabled") && !sizeSettings["Enabled"].is_boolean())
				warn("TribeSizeMultiplier.Enabled is not a boolean.");

			if (sizeSettings.contains("Multipliers"))
			{
				if (!sizeSettings["Multipliers"].is_array())
				{
					warn("TribeSizeMultiplier.Multipliers is not an array.");
				}
				else
				{
					std::unordered_set<int> configuredSizes;
					for (const auto& entry : sizeSettings["Multipliers"])
					{
						if (!entry.is_object())
						{
							warn("TribeSizeMultiplier.Multipliers contains a non-object entry.");
							continue;
						}

						if (!entry.contains("TribeSize") || !entry["TribeSize"].is_number_integer() ||
							entry["TribeSize"].get<int>() <= 0)
						{
							warn("TribeSizeMultiplier entry has a missing or invalid positive integer TribeSize.");
							continue;
						}

						const int tribeSize = entry["TribeSize"].get<int>();
						if (!configuredSizes.insert(tribeSize).second)
							warn("TribeSizeMultiplier has duplicate TribeSize " + std::to_string(tribeSize) + " — first matching entry is used.");

						if (!entry.contains("Multiplier") || !entry["Multiplier"].is_number() ||
							entry["Multiplier"].get<float>() <= 0.0f)
						{
							warn("TribeSizeMultiplier entry for TribeSize " + std::to_string(tribeSize) +
								" has a missing or invalid positive Multiplier.");
						}
					}
				}
			}
		}
	}

	// ---- 3c. ChangePlayerRate block -------------------------------------
	if (cfg.contains("ChangePlayerRate"))
	{
		const nlohmann::json& cpr = cfg["ChangePlayerRate"];
		if (!cpr.is_object())
		{
			warn("'ChangePlayerRate' is not a JSON object.");
		}
		else
		{
			if (cpr.contains("Enable") && !cpr["Enable"].is_boolean())
				warn("ChangePlayerRate.Enable is not a boolean.");
			if (cpr.contains("AlsoForTheTribe") && !cpr["AlsoForTheTribe"].is_boolean())
				warn("ChangePlayerRate.AlsoForTheTribe is not a boolean.");
			if (cpr.contains("Multiplier") && !cpr["Multiplier"].is_boolean())
				warn("ChangePlayerRate.Multiplier is not a boolean.");

			if (cpr.contains("HarvestAmount"))
			{
				if (!cpr["HarvestAmount"].is_number())
					warn("ChangePlayerRate.HarvestAmount is not a number.");
				else if (cpr["HarvestAmount"].get<float>() <= 0.0f)
					warn("ChangePlayerRate.HarvestAmount is <= 0 - changePlayerRate will reject boosts.");
			}

			if (cpr.contains("DurationMinutes"))
			{
				if (!cpr["DurationMinutes"].is_number_integer())
					warn("ChangePlayerRate.DurationMinutes must be an integer (minutes).");
				else if (cpr["DurationMinutes"].get<int64_t>() < 0)
					warn("ChangePlayerRate.DurationMinutes is < 0 - use 0 for a permanent boost.");
			}

			if (cpr.contains("ActivationMessage") && !cpr["ActivationMessage"].is_string())
				warn("ChangePlayerRate.ActivationMessage is not a string.");
			if (cpr.contains("ExpiryMessage") && !cpr["ExpiryMessage"].is_string())
				warn("ChangePlayerRate.ExpiryMessage is not a string.");
		}
	}



	// ---- 4. Schedule block validation ----------------------------------
	if (cfg.contains("Schedule"))
	{
		const nlohmann::json& schedule = cfg["Schedule"];

		if (!schedule.is_object())
		{
			warn("'Schedule' is not a JSON object.");
		}
		else
		{
			if (schedule.contains("Enabled") && !schedule["Enabled"].is_boolean())
				warn("Schedule.Enabled is not a boolean.");

			if (schedule.contains("CheckIntervalSeconds"))
			{
				if (!schedule["CheckIntervalSeconds"].is_number_integer())
					warn("Schedule.CheckIntervalSeconds is not an integer.");
				else if (schedule["CheckIntervalSeconds"].get<int>() < 1)
					warn("Schedule.CheckIntervalSeconds is < 1 — will be clamped to 1 second.");
			}

			if (schedule.contains("DefaultPreset"))
			{
				if (!schedule["DefaultPreset"].is_string())
					warn("Schedule.DefaultPreset is not a string.");
				else
				{
					const std::string dp = schedule["DefaultPreset"].get<std::string>();
					if (!dp.empty() && !cfg["RatePresets"].contains(dp))
						warn("Schedule.DefaultPreset '" + dp + "' does not exist in RatePresets.");
				}
			}

			if (!schedule.contains("Rules") || !schedule["Rules"].is_array())
			{
				warn("Schedule.Rules is missing or not an array.");
			}
			else
			{
				const std::vector<std::string> validDays = {
					"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
				};
				auto isValidDay = [&](const std::string& d) {
					for (const auto& v : validDays) if (v == d) return true;
					return false;
				};

				int ruleIdx = 0;
				for (const auto& rule : schedule["Rules"])
				{
					const std::string ruleId = "Schedule.Rules[" + std::to_string(ruleIdx++) + "]";

					// Enabled field (optional — both formats)
					if (rule.contains("Enabled") && !rule["Enabled"].is_boolean())
						warn(ruleId + ": 'Enabled' must be a boolean.");

					// Preset field (required — both formats)
					if (!rule.contains("Preset") || !rule["Preset"].is_string())
					{
						warn(ruleId + ": missing or invalid 'Preset' field.");
						continue;
					}
					const std::string rulePreset = rule["Preset"].get<std::string>();
					if (!cfg["RatePresets"].contains(rulePreset))
						warn(ruleId + ": Preset '" + rulePreset + "' does not exist in RatePresets.");

					// Determine format and validate accordingly
					const bool hasStartDay = rule.contains("StartDay");
					const bool hasEndDay   = rule.contains("EndDay");
					const bool hasDaysArr  = rule.contains("Days");

					if (hasStartDay || hasEndDay)
					{
						// ---- NEW day-range format ----------------------------------------
						if (!hasStartDay || !hasEndDay)
							warn(ruleId + ": 'StartDay' and 'EndDay' must both be present.");
						else
						{
							if (!rule["StartDay"].is_string() || !isValidDay(rule["StartDay"].get<std::string>()))
								warn(ruleId + ": 'StartDay' is not a valid day name.");
							if (!rule["EndDay"].is_string() || !isValidDay(rule["EndDay"].get<std::string>()))
								warn(ruleId + ": 'EndDay' is not a valid day name.");
						}

						if (!rule.contains("StartHour") || !rule.contains("EndHour"))
						{
							warn(ruleId + ": day-range rule requires 'StartHour' and 'EndHour'.");
						}
						else if (!rule["StartHour"].is_number_integer() || !rule["EndHour"].is_number_integer())
						{
							warn(ruleId + ": 'StartHour' and 'EndHour' must be integers.");
						}
						else
						{
							const int sh = rule["StartHour"].get<int>();
							const int eh = rule["EndHour"].get<int>();
							if (sh < 0 || sh > 23)
								warn(ruleId + ": StartHour " + std::to_string(sh) + " out of range [0,23].");
							if (eh < 0 || eh > 23)
								warn(ruleId + ": EndHour " + std::to_string(eh) + " out of range [0,23].");
						}

						if (hasDaysArr)
							warn(ruleId + ": has both 'StartDay'/'EndDay' and 'Days' — 'Days' will be ignored (day-range format takes priority).");
					}
					else if (hasDaysArr)
					{
						// ---- OLD Days-array format -------------------------------------------
						if (!rule["Days"].is_array())
						{
							warn(ruleId + ": 'Days' must be an array.");
						}
						else
						{
							for (const auto& day : rule["Days"])
							{
								if (!day.is_string())
									warn(ruleId + ": 'Days' contains a non-string entry.");
								else if (!isValidDay(day.get<std::string>()))
									warn(ruleId + ": '" + day.get<std::string>() + "' is not a valid day name.");
							}
						}

						if (!rule.contains("StartHour") || !rule.contains("EndHour"))
						{
							warn(ruleId + ": missing 'StartHour' or 'EndHour'.");
						}
						else if (!rule["StartHour"].is_number_integer() || !rule["EndHour"].is_number_integer())
						{
							warn(ruleId + ": 'StartHour' and 'EndHour' must be integers.");
						}
						else
						{
							const int sh = rule["StartHour"].get<int>();
							const int eh = rule["EndHour"].get<int>();
							if (sh < 0 || sh > 23)
								warn(ruleId + ": StartHour " + std::to_string(sh) + " out of range [0,23].");
							if (eh < 0 || eh > 23)
								warn(ruleId + ": EndHour " + std::to_string(eh) + " out of range [0,23].");
							if (sh > eh)
								warn(ruleId + ": StartHour > EndHour — overnight ranges are not supported in the Days-array format.");
						}
					}
					else
					{
						warn(ruleId + ": must have either 'StartDay'+'EndDay' (day-range) or 'Days' (legacy) fields.");
					}
				}
			}
		}
	}

	// ---- 5. Summary ----------------------------------------------------
	if (issueCount == 0)
		Log::GetLog()->info("ValidateConfig: config.json passed validation with no issues.");
	else
		Log::GetLog()->warn("ValidateConfig: {} issue(s) found — review warnings above.", issueCount);
}

// ---------------------------------------------------------------------------
// GetCurrentSchedulePreset
//   Evaluates Schedule rules against the current server clock and returns
//   the name of the first matching preset, or an empty string if no rule
//   matches (or if the Schedule is disabled / not configured).
//
//   Supports TWO rule formats — both can coexist in the same Rules array:
//
//   NEW — Day-range format (recommended):
//     { "Preset":"weekend_rates", "StartDay":"Friday", "StartHour":16,
//                                 "EndDay":"Sunday",   "EndHour":16 }
//
//     The range [StartDay:StartHour, EndDay:EndHour] is evaluated as a
//     week-position: weekPos = dayIndex × 24 + hour (0=Sunday 0h … 167=Saturday 23h).
//     If startPos ≤ endPos → normal range within the week.
//     If startPos > endPos → range wraps over the Sunday boundary (e.g. Fri→Sun).
//
//   OLD — Days-array format (still fully supported):
//     { "Preset":"normal_rates", "Days":["Mon","Tue"], "StartHour":0, "EndHour":17 }
//
//   Per-rule "Enabled" field (both formats):
//     Set "Enabled": false on any rule to skip it without deleting it.
//     Defaults to true when the field is absent.
//
//   Used by CheckSchedule (Timers.h) and the timed-preset expiry callback.
// ---------------------------------------------------------------------------
std::string GetCurrentSchedulePreset()
{
	if (!CousinCustomRates::config.contains("Schedule")) return "";

	const nlohmann::json& schedule = CousinCustomRates::config["Schedule"];
	if (!schedule.value("Enabled", false))   return "";
	if (!schedule.contains("Rules") || !schedule["Rules"].is_array()) return "";

	// Function-local static — initialised once, no linker conflicts with Timers.h
	static const std::unordered_map<std::string, int> s_dayMap = {
		{"Sunday",0},{"Monday",1},{"Tuesday",2},{"Wednesday",3},
		{"Thursday",4},{"Friday",5},{"Saturday",6}
	};

	std::time_t now = std::time(nullptr);
	std::tm localTime{};
#if defined(_WIN32)
	localtime_s(&localTime, &now);
#else
	localtime_r(&now, &localTime);
#endif

	const int currentHour    = localTime.tm_hour; // 0-23
	const int currentWDay    = localTime.tm_wday; // 0=Sunday … 6=Saturday
	const int currentWeekPos = currentWDay * 24 + currentHour; // 0-167

	for (const auto& rule : schedule["Rules"])
	{
		// Skip rules that are explicitly disabled
		if (!rule.value("Enabled", true)) continue;

		if (!rule.contains("Preset")) continue;
		const std::string target = rule.value("Preset", "");
		if (target.empty()) continue;

		bool matches = false;

		if (rule.contains("StartDay") && rule.contains("EndDay"))
		{
			// ---- NEW day-range format ----------------------------------------
			if (!rule.contains("StartHour") || !rule.contains("EndHour")) continue;

			auto startIt = s_dayMap.find(rule.value("StartDay", ""));
			auto endIt   = s_dayMap.find(rule.value("EndDay",   ""));
			if (startIt == s_dayMap.end() || endIt == s_dayMap.end()) continue;

			const int startPos = startIt->second * 24 + rule.value("StartHour", 0);
			const int endPos   = endIt->second   * 24 + rule.value("EndHour",   23);

			if (startPos <= endPos)
				// Normal range within the week (e.g. Mon 8h → Fri 18h)
				matches = (currentWeekPos >= startPos && currentWeekPos <= endPos);
			else
				// Wraps the Sunday boundary (e.g. Fri 16h → Sun 16h)
				matches = (currentWeekPos >= startPos || currentWeekPos <= endPos);
		}
		else if (rule.contains("Days"))
		{
			// ---- OLD Days-array format (backward compatible) -----------------
			if (!rule.contains("StartHour") || !rule.contains("EndHour")) continue;

			const int startHour = rule.value("StartHour", 0);
			const int endHour   = rule.value("EndHour",   23);

			if (currentHour < startHour || currentHour > endHour) continue;

			for (const auto& dayEntry : rule["Days"])
			{
				if (!dayEntry.is_string()) continue;
				auto it = s_dayMap.find(dayEntry.get<std::string>());
				if (it != s_dayMap.end() && it->second == currentWDay) { matches = true; break; }
			}
		}
		// else: unknown format — skip silently

		if (matches) return target;
	}

	// No rule matched — fall back to DefaultPreset if configured
	return schedule.value("DefaultPreset", "");
}

// ---------------------------------------------------------------------------
// ArmTimedPresetExpiry
//   Schedules a one-shot API::Timer::DelayExecute to fire after durationSeconds.
//   When it fires:
//     1. Clears the in-memory timed state (expiry + fallback).
//     2. Checks if a schedule rule currently applies — if so, that preset is
//        used as the revert target; otherwise the captured fallback preset is used.
//     3. Calls ApplyRates with fromTimedExpiry=true (so it does NOT re-arm
//        another timed countdown, even if the fallback preset has a Duration field).
//
//   The identifier "CousinCustomRatesTimedPreset" lets any subsequent
//   changerates command cancel the timer via UnloadTimer before it fires.
// ---------------------------------------------------------------------------
void ArmTimedPresetExpiry(int64_t durationSeconds)
{
	// API::Timer::DelayExecute takes an int — clamp safely
	const int delayInt = (durationSeconds > 2147483647LL)
		? 2147483647
		: (durationSeconds < 1 ? 1 : static_cast<int>(durationSeconds));

	API::Timer::Get().DelayExecute(
		"CousinCustomRatesTimedPreset",
		[]()
		{
			Log::GetLog()->info("TimedPreset: countdown expired — determining revert target.");

			// Clear timed state
			CousinCustomRates::timedPresetExpiry = 0;

			// Decide where to revert: schedule takes priority over captured fallback
			const std::string schedulePreset = GetCurrentSchedulePreset();
			const std::string target = !schedulePreset.empty()
				? schedulePreset
				: CousinCustomRates::timedFallbackPreset;

			CousinCustomRates::timedFallbackPreset.clear();

			if (target.empty())
			{
				Log::GetLog()->warn("TimedPreset: no revert target found — rates unchanged.");
				return;
			}

			Log::GetLog()->info("TimedPreset: reverting to '{}'{}.",
				target,
				!schedulePreset.empty() ? " (schedule match)" : " (captured fallback)");

			// fromTimedExpiry=true — prevents re-arming a new timer even if the
			// revert target itself has a Duration field in config.
			ApplyRates(FString(target.c_str()), true, true);
		},
		delayInt
	);

	Log::GetLog()->info("ArmTimedPresetExpiry: timer armed ({}s).", delayInt);
}

// ---------------------------------------------------------------------------
// ReadConfig
//   Loads config.json from the plugin folder into CousinCustomRates::config.
//   Calls ValidateConfig() after parsing to log any structural issues.
// ---------------------------------------------------------------------------
void ReadConfig()
{
	try
	{
		const std::string config_path =
			AsaApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/" + PROJECT_NAME + "/config.json";

		std::ifstream file{ config_path };
		if (!file.is_open())
			throw std::runtime_error("Cannot open config file: " + config_path);

		file >> CousinCustomRates::config;

		Log::GetLog()->info("{} config loaded successfully.", PROJECT_NAME);

		// Validate structure and log any issues — never throws.
		ValidateConfig();
	}
	catch (const std::exception& error)
	{
		Log::GetLog()->error("ReadConfig failed. ERROR: {}", error.what());
		throw;
	}
}

// ---------------------------------------------------------------------------
// SaveState
//   Persists the active preset name (and timed preset state, if active)
//   to status.json so both survive server restarts.
// ---------------------------------------------------------------------------
void SaveState(const std::string& presetName)
{
	try
	{
		const std::string path =
			AsaApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/" + PROJECT_NAME + "/status.json";

		nlohmann::json stateJson;
		stateJson["active_preset"] = presetName;

		// Persist timed global-preset info only when a countdown is active.
		if (CousinCustomRates::timedPresetExpiry > 0)
		{
			stateJson["timed_expiry"]   = CousinCustomRates::timedPresetExpiry;
			stateJson["timed_fallback"] = CousinCustomRates::timedFallbackPreset;
		}

		// Persist every player harvest boost in the same document. This is
		// deliberately independent from the one global timed-preset state.
		stateJson["player_harvest_boosts"] = nlohmann::json::array();
		for (const auto& [eosId, boost] : CousinCustomRates::playerHarvestBoosts)
		{
			stateJson["player_harvest_boosts"].push_back({
				{"eos_id", boost.targetEosId},
				{"team_id", boost.teamId},
				{"player_data_id", boost.playerDataId},
				{"harvest_amount", boost.harvestAmount},
				{"is_multiplier", boost.isMultiplier},
				{"also_for_tribe", boost.alsoForTribe},
				{"expiry", boost.expiryUnixTime}
			});
		}

		stateJson["tribe_member_counts"] = nlohmann::json::array();
		for (const auto& [teamId, memberCount] : CousinCustomRates::tribeMemberCountsByTeam)
		{
			if (teamId != 0 && memberCount > 0)
			{
				stateJson["tribe_member_counts"].push_back({
					{"team_id", teamId},
					{"member_count", memberCount}
				});
			}
		}

		std::ofstream file{ path };
		if (!file.is_open())
		{
			Log::GetLog()->error("SaveState: cannot open {} for writing.", path);
			return;
		}
		file << stateJson.dump(2);

		Log::GetLog()->info("SaveState: preset '{}' saved to status.json.", presetName);
	}
	catch (const std::exception& error)
	{
		Log::GetLog()->error("SaveState failed. ERROR: {}", error.what());
	}
}

// ---------------------------------------------------------------------------
// LoadState
//   Reads status.json, returns the last active preset name, and populates
//   the timed preset globals (timedPresetExpiry, timedFallbackPreset) so
//   Hooks.h can decide whether to resume or expire the countdown on restart.
//   Returns an empty string if the file does not exist or is invalid.
// ---------------------------------------------------------------------------
std::string LoadState()
{
	try
	{
		const std::string path =
			AsaApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/" + PROJECT_NAME + "/status.json";

		std::ifstream file{ path };
		if (!file.is_open())
			return "";

		nlohmann::json stateJson;
		file >> stateJson;

		// Restore timed preset state into globals (may be 0 / empty if not saved).
		CousinCustomRates::timedPresetExpiry   = stateJson.value("timed_expiry",   static_cast<int64_t>(0));
		CousinCustomRates::timedFallbackPreset = stateJson.value("timed_fallback", std::string(""));

		// Restore valid, non-expired player harvest boosts. A boost that expired
		// while the server was offline simply falls back to current global rates.
		CousinCustomRates::playerHarvestBoosts.clear();
		const int64_t now = static_cast<int64_t>(std::time(nullptr));
		if (stateJson.contains("player_harvest_boosts") && stateJson["player_harvest_boosts"].is_array())
		{
			for (const auto& entry : stateJson["player_harvest_boosts"])
			{
				if (!entry.is_object()) continue;
				const std::string eosId = entry.value("eos_id", "");
				const float amount = entry.value("harvest_amount", 0.0f);
				const int64_t expiry = entry.value("expiry", static_cast<int64_t>(0));
				if (eosId.empty() || amount <= 0.0f || (expiry > 0 && expiry <= now))
					continue;

				CousinCustomRates::PlayerHarvestBoost boost;
				boost.targetEosId = eosId;
				boost.teamId = entry.value("team_id", 0);
				boost.playerDataId = entry.value("player_data_id", static_cast<unsigned long long>(0));
				boost.harvestAmount = amount;
				boost.isMultiplier = entry.value("is_multiplier", true);
				boost.alsoForTribe = entry.value("also_for_tribe", true);
				boost.expiryUnixTime = expiry;
				CousinCustomRates::playerHarvestBoosts[eosId] = boost;
			}
		}

		// Legacy migration: pre-redesign team-keyed boosts become tribe-wide
		// fixed-rate boosts (they were absolute rates before the redesign).
		else if (stateJson.contains("tribe_harvest_boosts") && stateJson["tribe_harvest_boosts"].is_array())
		{
			for (const auto& entry : stateJson["tribe_harvest_boosts"])
			{
				if (!entry.is_object()) continue;
				const int teamId = entry.value("team_id", 0);
				const float multiplier = entry.value("harvest_multiplier", 0.0f);
				const int64_t expiry = entry.value("expiry", static_cast<int64_t>(0));
				if (teamId == 0 || multiplier <= 0.0f || (expiry > 0 && expiry <= now))
					continue;

				CousinCustomRates::PlayerHarvestBoost boost;
				boost.targetEosId = entry.value("notification_eos_id", "");
				boost.teamId = teamId;
				boost.harvestAmount = multiplier;
				boost.isMultiplier = false;
				boost.alsoForTribe = true;
				boost.expiryUnixTime = expiry;

				const std::string key = !boost.targetEosId.empty()
					? boost.targetEosId
					: "legacy_team_" + std::to_string(teamId);
				CousinCustomRates::playerHarvestBoosts[key] = boost;
			}

			Log::GetLog()->info("LoadState: migrated legacy tribe_harvest_boosts entries.");
		}

		CousinCustomRates::tribeMemberCountsByTeam.clear();
		if (stateJson.contains("tribe_member_counts") && stateJson["tribe_member_counts"].is_array())
		{
			for (const auto& entry : stateJson["tribe_member_counts"])
			{
				if (!entry.is_object()) continue;
				const int teamId = entry.value("team_id", 0);
				const int memberCount = entry.value("member_count", 0);
				if (teamId != 0 && memberCount > 0)
					CousinCustomRates::tribeMemberCountsByTeam[teamId] = memberCount;
			}
		}

		return stateJson.value("active_preset", "");
	}
	catch (const std::exception& error)
	{
		Log::GetLog()->warn("LoadState: could not read status.json. ERROR: {}", error.what());
		return "";
	}
}

// ---------------------------------------------------------------------------
// SendMessageToDiscordCallback
//   Callback invoked after the webhook POST completes.
// ---------------------------------------------------------------------------
void SendMessageToDiscordCallback(bool success, std::string results,
	std::unordered_map<std::string, std::string> /*responseHeaders*/)
{
	if (!success)
		Log::GetLog()->error("Discord webhook POST failed. Response: {}", results);
	else
		Log::GetLog()->info("Discord webhook POST succeeded.");
}

// ---------------------------------------------------------------------------
// SendMessageToDiscord
//   Posts a Discord message to a webhook URL.
//
//   If the preset has a "Discord_Embed" block in config.json, a rich embed
//   is built with a colored sidebar, title, description, auto-populated rate
//   fields, and an optional footer.
//
//   If no "Discord_Embed" block is present, a plain content string is sent
//   as fallback.
//
//   If webhookUrl is empty this function does nothing — no error is thrown.
//
//   Config structure (all sub-fields are optional):
//   "Discord_Embed": {
//     "Title":       "⚡ Weekend Rates Activated",
//     "Description": "Server is now running boosted rates!",
//     "Color":       3066993,      <- decimal RGB (e.g. 3066993 = green)
//     "Footer":      "CousinCustomRates"
//   }
// ---------------------------------------------------------------------------
void SendMessageToDiscord(const std::string& webhookUrl,
	const std::string& presetKey,
	const nlohmann::json& preset)
{
	if (webhookUrl.empty())
		return;  // Webhook not configured — skip silently

	try
	{
		nlohmann::json payload;

		if (preset.contains("Discord_Embed"))
		{
			// ---- Build a rich embed ----------------------------------------
			const nlohmann::json& embedCfg = preset["Discord_Embed"];

			nlohmann::json embed;
			embed["title"]       = embedCfg.value("Title",       "Rate Change");
			embed["description"] = embedCfg.value("Description", "A new rate preset has been activated.");
			embed["color"]       = embedCfg.value("Color",       3447003); // default: blue

			// Rate fields — always auto-populated from the preset multipliers
			embed["fields"] = nlohmann::json::array({
				{ {"name","Preset"},      {"value", presetKey},                                                                     {"inline", false} },
				{ {"name","Taming"},      {"value", fmt::format("{}x", preset.value("TamingSpeedMultiplier",     1.0f))},           {"inline", true } },
				{ {"name","XP"},          {"value", fmt::format("{}x", preset.value("XPMultiplier",              1.0f))},           {"inline", true } },
				{ {"name","Harvest"},     {"value", fmt::format("{}x", preset.value("HarvestAmountMultiplier",   1.0f))},           {"inline", true } },
				{ {"name","Baby Mature"}, {"value", fmt::format("{}x", preset.value("BabyMatureSpeedMultiplier", 1.0f))},           {"inline", true } },
				{ {"name","Egg Hatch"},   {"value", fmt::format("{}x", preset.value("EggHatchSpeedMultiplier",   1.0f))},           {"inline", true } }
			});

			if (embedCfg.contains("Footer"))
				embed["footer"] = { {"text", embedCfg.value("Footer", "")} };

			payload["embeds"] = nlohmann::json::array({ embed });
		}
		else
		{
			// ---- Fallback: plain text message --------------------------------
			payload["content"] = fmt::format(
				"[{}] Rate preset **{}** activated. "
				"Taming: {}x | XP: {}x | Harvest: {}x | Baby: {}x | Egg: {}x",
				PROJECT_NAME,
				presetKey,
				preset.value("TamingSpeedMultiplier",     1.0f),
				preset.value("XPMultiplier",              1.0f),
				preset.value("HarvestAmountMultiplier",   1.0f),
				preset.value("BabyMatureSpeedMultiplier", 1.0f),
				preset.value("EggHatchSpeedMultiplier",   1.0f)
			);
			payload["username"] = "CousinCustomRates";
		}

		const std::string body = payload.dump();

		std::vector<std::string> headers = {
			"Content-Type: application/json",
			"User-Agent: CousinCustomRates/1.0"
		};

		bool ok = CousinCustomRates::req.CreatePostRequest(
			webhookUrl,
			&SendMessageToDiscordCallback,
			body,
			"application/json",
			headers
		);

		if (!ok)
			Log::GetLog()->error("SendMessageToDiscord: CreatePostRequest returned false.");
	}
	catch (const std::exception& error)
	{
		Log::GetLog()->error("SendMessageToDiscord failed. ERROR: {}", error.what());
	}
}

// ---------------------------------------------------------------------------
// BroadcastRateChange
//   Sends a server-wide on-screen HUD notification to all connected players.
// ---------------------------------------------------------------------------
void BroadcastRateChange(const std::string& message)
{
	if (message.empty())
		return;

	FLinearColor color(1.0f, 0.9f, 0.0f, 1.0f); // yellow
	AsaApi::GetApiUtils().SendNotificationToAll(color, 1.5f, 10.0f, nullptr, "{}", message.c_str());
}

// ---------------------------------------------------------------------------
// ApplyRates
//   Looks up presetName in config.json, writes all five multipliers into both
//   the live AShooterGameMode (server authority) and AShooterGameState
//   (replicated to clients), then forces a net update.
//
//   Parameters:
//     presetName         — key in config.json RatePresets
//     sendNotifications  — if false, skip in-game broadcast and Discord webhook
//                          (used on server restart to silently restore rates)
//     fromTimedExpiry    — if true, skip all timed-preset logic (used when
//                          the expiry callback applies the fallback, and when
//                          the InitGame hook restores state on restart)
//
//   Timed Preset Logic (when fromTimedExpiry == false):
//     - Any running timed countdown is always cancelled first.
//     - If the preset has a positive "Duration" field AND TimedPresets.Enabled
//       is true: captures the fallback (first activation only), persists the
//       expiry timestamp, and arms a DelayExecute one-shot timer.
//     - Otherwise: clears the timed state.
//
//   Returns true on success, false if the preset was not found or if the
//   GameMode / GameState pointer is not yet available.
// ---------------------------------------------------------------------------
bool ApplyRates(const FString& presetName, bool sendNotifications, bool fromTimedExpiry,
	AShooterGameMode* gameModeOverride)
{
	const std::string presetKey = presetName.ToString();

	// Validate preset exists in config
	if (!CousinCustomRates::config.contains("RatePresets") ||
		!CousinCustomRates::config["RatePresets"].contains(presetKey))
	{
		Log::GetLog()->error("ApplyRates: preset '{}' not found in config.json.", presetKey);
		return false;
	}

	// Commands and timers resolve the live GameMode through AsaApi. Startup
	// restoration passes the BeginPlay hook's known-valid instance directly.
	AShooterGameMode* gameMode = gameModeOverride
		? gameModeOverride
		: AsaApi::GetApiUtils().GetShooterGameMode();
	if (!gameMode)
	{
		Log::GetLog()->error("ApplyRates: AShooterGameMode is null — server not ready yet.");
		return false;
	}

	// Get the GameState from the GameMode's generated field. GetGameState() is
	// not part of the official ASA ApiUtils interface.
	AGameStateBase* gameStateBase = gameMode->GameStateField().Get();
	if (!gameStateBase)
	{
		Log::GetLog()->error("ApplyRates: GameMode.GameState is null.");
		return false;
	}

	if (!gameStateBase->IsA(AShooterGameState::StaticClass()))
	{
		Log::GetLog()->error("ApplyRates: GameMode.GameState is not an AShooterGameState.");
		return false;
	}

	AShooterGameState* gameState = static_cast<AShooterGameState*>(gameStateBase);

	const nlohmann::json& preset = CousinCustomRates::config["RatePresets"][presetKey];

	// ---------------------------------------------------------------------------
	// Timed Preset Logic
	//   Always cancel any existing countdown (safe even if none is running).
	//   Then, unless this call itself came from the expiry callback, decide
	//   whether to arm a new countdown.
	// ---------------------------------------------------------------------------
	API::Timer::Get().UnloadTimer("CousinCustomRatesTimedPreset");

	if (!fromTimedExpiry)
	{
		const bool timedEnabled =
			CousinCustomRates::config.contains("TimedPresets") &&
			CousinCustomRates::config["TimedPresets"].value("Enabled", false);

		const bool hasDuration =
			preset.contains("Duration") &&
			preset["Duration"].is_number_integer() &&
			preset["Duration"].get<int64_t>() > 0;

		if (timedEnabled && hasDuration)
		{
			// Duration is stored in MINUTES in config — convert to seconds internally
			const int64_t durationMinutes = preset["Duration"].get<int64_t>();
			const int64_t durationSeconds = durationMinutes * 60LL;

			// Capture the fallback only on the FIRST timed preset activation.
			// If a timed preset is already running, keep the original fallback
			// so stacked timed presets always revert to the original state.
			if (CousinCustomRates::timedPresetExpiry == 0)
				CousinCustomRates::timedFallbackPreset = CousinCustomRates::lastPreset;

			// Warn immediately if the fallback preset no longer exists in config
			if (!CousinCustomRates::timedFallbackPreset.empty() &&
				!CousinCustomRates::config["RatePresets"].contains(CousinCustomRates::timedFallbackPreset))
			{
				Log::GetLog()->warn(
					"ApplyRates: timed fallback preset '{}' not found in RatePresets — "
					"expiry will have no fallback unless a schedule rule matches.",
					CousinCustomRates::timedFallbackPreset);
			}

			CousinCustomRates::timedPresetExpiry =
				static_cast<int64_t>(std::time(nullptr)) + durationSeconds;

			ArmTimedPresetExpiry(durationSeconds);

			Log::GetLog()->info(
				"ApplyRates: timed preset '{}' armed — expires in {} minute(s) ({}s), fallback='{}'.",
				presetKey, durationMinutes, durationSeconds, CousinCustomRates::timedFallbackPreset);
		}
		else
		{
			// Normal preset — clear any stale timed state
			CousinCustomRates::timedPresetExpiry = 0;
			CousinCustomRates::timedFallbackPreset.clear();
		}
	}
	// else (fromTimedExpiry): timed state already managed by caller — don't touch it

	// ---------------------------------------------------------------------------
	// Apply multipliers
	// ---------------------------------------------------------------------------

	// 1. Update GameMode — server-side authority
	gameMode->TamingSpeedMultiplierField()     = preset.value("TamingSpeedMultiplier",     1.0f);
	gameMode->XPMultiplierField()              = preset.value("XPMultiplier",              1.0f);
	gameMode->HarvestAmountMultiplierField()   = preset.value("HarvestAmountMultiplier",   1.0f);
	gameMode->BabyMatureSpeedMultiplierField() = preset.value("BabyMatureSpeedMultiplier", 1.0f);
	gameMode->EggHatchSpeedMultiplierField()   = preset.value("EggHatchSpeedMultiplier",   1.0f);

	// Auto-scale cuddle interval when maturation is boosted
	{
		const float matureSpeed = preset.value("BabyMatureSpeedMultiplier", 1.0f);
		const float cuddleInterval = preset.contains("BabyCuddleIntervalMultiplier")
			? preset.value("BabyCuddleIntervalMultiplier", 1.0f)
			: (matureSpeed > 1.0f ? 1.0f / matureSpeed : 1.0f);

		gameMode->BabyCuddleIntervalMultiplierField() = cuddleInterval;
	}

	gameMode->bUseSingleplayerSettingsField() = false;

	// 2. Update GameState — replicated to clients
	gameState->EggHatchSpeedMultiplierField() = preset.value("EggHatchSpeedMultiplier", 1.0f);

	// 3. Force net update
	gameState->ForceNetUpdate(false, true, false);

	Log::GetLog()->info(
		"ApplyRates: preset '{}' applied. "
		"Taming={} XP={} Harvest={} BabyMature={} EggHatch={}",
		presetKey,
		preset.value("TamingSpeedMultiplier",     1.0f),
		preset.value("XPMultiplier",              1.0f),
		preset.value("HarvestAmountMultiplier",   1.0f),
		preset.value("BabyMatureSpeedMultiplier", 1.0f),
		preset.value("EggHatchSpeedMultiplier",   1.0f)
	);

	// Persist active preset (and timed state) for restart survival
	CousinCustomRates::lastPreset = presetKey;
	SaveState(presetKey);

	// In-game broadcast and Discord notification — skipped on silent restores
	// and when called from the timed expiry callback (which will have its own
	// notifications via the normal sendNotifications=true path).
	if (sendNotifications)
	{
		BroadcastRateChange(preset.value("BroadcastMessage", ""));
		SendMessageToDiscord(preset.value("Discord_Webhook", ""), presetKey, preset);
	}

	return true;
}
