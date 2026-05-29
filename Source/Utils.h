#pragma once

#include <fstream>
#include <string>

// ---------------------------------------------------------------------------
// ReadConfig
//   Loads config.json from the plugin folder into CousinCustomRates::config.
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
	}
	catch (const std::exception& error)
	{
		Log::GetLog()->error("ReadConfig failed. ERROR: {}", error.what());
		throw;
	}
}

// ---------------------------------------------------------------------------
// SaveState
//   Persists the active preset name to status.json so it survives restarts.
// ---------------------------------------------------------------------------
void SaveState(const std::string& presetName)
{
	try
	{
		const std::string path =
			AsaApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/" + PROJECT_NAME + "/status.json";

		nlohmann::json stateJson;
		stateJson["active_preset"] = presetName;

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
//   Reads status.json and returns the last active preset name.
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
//   Uses SendNotificationToAll so the message appears as a pop-up overlay
//   rather than in the chat box.
//
//   Parameters used:
//     color         - yellow, so it stands out against most backgrounds
//     display_scale - 1.5f  (slightly larger than default for visibility)
//     display_time  - 10.0f (seconds the notification stays on screen)
//     icon          - nullptr (no custom icon)
//
//   Skips silently if message is empty.
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
//   (replicated to clients / used by world ticking logic), then forces a net
//   update so clients see the changes immediately.
//
//   After that it saves state, sends a Discord message (rich embed or plain
//   text), and broadcasts in-game.
//
//   Returns true on success, false if the preset was not found or if the
//   GameMode / GameState pointer is not yet available.
// ---------------------------------------------------------------------------
bool ApplyRates(const FString& presetName, bool sendNotifications = true)
{
	const std::string presetKey = presetName.ToString();

	// Validate preset exists in config
	if (!CousinCustomRates::config.contains("RatePresets") ||
		!CousinCustomRates::config["RatePresets"].contains(presetKey))
	{
		Log::GetLog()->error("ApplyRates: preset '{}' not found in config.json.", presetKey);
		return false;
	}

	// Get live GameMode pointer
	AShooterGameMode* gameMode = AsaApi::GetApiUtils().GetShooterGameMode();
	if (!gameMode)
	{
		Log::GetLog()->error("ApplyRates: AShooterGameMode is null — server not ready yet.");
		return false;
	}

	// Get live GameState pointer (replicated to all clients).
	// Use IApiUtils::GetGameState() which reads directly from UWorld::GameState —
	// avoids the "Failed to get offset of AGameModeBase.GetGameState" runtime crash
	// that occurs when calling GetGameState() through the gameMode pointer.
	AShooterGameState* gameState = AsaApi::GetApiUtils().GetGameState();
	if (!gameState)
	{
		Log::GetLog()->error("ApplyRates: AShooterGameState is null.");
		return false;
	}

	const nlohmann::json& preset = CousinCustomRates::config["RatePresets"][presetKey];

	// 1. Update GameMode — server-side authority
	gameMode->TamingSpeedMultiplierField()     = preset.value("TamingSpeedMultiplier",     1.0f);
	gameMode->XPMultiplierField()              = preset.value("XPMultiplier",              1.0f);
	gameMode->HarvestAmountMultiplierField()   = preset.value("HarvestAmountMultiplier",   1.0f);
	gameMode->BabyMatureSpeedMultiplierField() = preset.value("BabyMatureSpeedMultiplier", 1.0f);
	gameMode->EggHatchSpeedMultiplierField()   = preset.value("EggHatchSpeedMultiplier",   1.0f);

	// Maturation helper: when babies mature faster, cuddle (imprinting) intervals must
	// scale inversely so imprinting remains achievable at high maturation speeds.
	// Only adjust automatically if "BabyCuddleIntervalMultiplier" is not explicitly set
	// in the preset — an explicit value in config always wins.
	{
		const float matureSpeed = preset.value("BabyMatureSpeedMultiplier", 1.0f);
		const float cuddleInterval = preset.contains("BabyCuddleIntervalMultiplier")
			? preset.value("BabyCuddleIntervalMultiplier", 1.0f)
			: (matureSpeed > 1.0f ? 1.0f / matureSpeed : 1.0f);

		gameMode->BabyCuddleIntervalMultiplierField() = cuddleInterval;
	}

	// Ensure singleplayer overrides are disabled so the live GameMode multipliers
	// are respected by the maturation tick logic.
	gameMode->bUseSingleplayerSettingsField() = false;

	// 2. Update GameState — used by world ticking logic and replicated to clients.
	// Note: BabyMatureSpeedMultiplierField does NOT exist in AShooterGameState;
	//       it is authoritative only in GameMode (already set above).
	//       EggHatchSpeedMultiplierField exists in both classes and must be mirrored
	//       here so the client UI and incubation ticking stay in sync.
	gameState->EggHatchSpeedMultiplierField() = preset.value("EggHatchSpeedMultiplier", 1.0f);

	// 3. Force a network update so clients receive the new GameState values immediately
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

	// Persist choice for server restarts
	CousinCustomRates::lastPreset = presetKey;
	SaveState(presetKey);

	// In-game broadcast and Discord notification are only sent when rates are
	// actively changed (e.g. via RCON or the scheduler), NOT when the plugin
	// silently restores the saved preset on server restart.
	if (sendNotifications)
	{
		// In-game broadcast (optional — empty string skips)
		BroadcastRateChange(preset.value("BroadcastMessage", ""));

		// Discord notification — rich embed if configured, plain text otherwise.
		// Empty webhook URL skips silently.
		SendMessageToDiscord(preset.value("Discord_Webhook", ""), presetKey, preset);
	}

	return true;
}
