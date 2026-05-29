#pragma once

#include <algorithm>   // std::find_if
#include <cctype>      // std::isspace

// ---------------------------------------------------------------------------
// ChangeRatesRcon
//
// RCON command: changerates <preset_name>
//   e.g.  changerates weekend_rates
//         changerates normal_rates
//
// Applies the named preset from config.json and persists the choice so it
// survives server restarts.
// ---------------------------------------------------------------------------
void ChangeRatesRcon(RCONClientConnection* rcon_connection, RCONPacket* rcon_packet, UWorld*)
{
	FString reply;

	// rcon_packet->Body contains the full command string, e.g. "changerates weekend_rates"
	// We need to extract everything after the first space.
	FString fullCmd = rcon_packet->Body;
	FString presetName;

	int32 spaceIdx = -1;
	if (fullCmd.FindChar(L' ', spaceIdx) && spaceIdx != INDEX_NONE)
	{
		// Everything after the first space is the preset name — trim manually
		std::string raw = fullCmd.Mid(spaceIdx + 1).ToString();

		// ltrim
		raw.erase(raw.begin(), std::find_if(raw.begin(), raw.end(),
			[](unsigned char c) { return !std::isspace(c); }));
		// rtrim
		raw.erase(std::find_if(raw.rbegin(), raw.rend(),
			[](unsigned char c) { return !std::isspace(c); }).base(), raw.end());

		presetName = FString(raw.c_str());
	}

	if (presetName.IsEmpty())
	{
		// List available presets as a hint
		reply = "Usage: changerates <preset_name>\nAvailable presets:";

		if (CousinCustomRates::config.contains("RatePresets"))
		{
			for (auto& [key, val] : CousinCustomRates::config["RatePresets"].items())
			{
				// Use FString concatenation — avoids mixing wide format strings
				// with narrow const char* arguments which can produce garbled text.
				reply += FString("\n  - ");
				reply += FString(key.c_str());
			}
		}

		rcon_connection->SendMessageW(rcon_packet->Id, 0, &reply);
		return;
	}

	Log::GetLog()->info("ChangeRatesRcon: received request to apply preset '{}'.",
		presetName.ToString());

	if (ApplyRates(presetName))
	{
		// FString concatenation — avoids wide/narrow string mixing
		reply = FString("Rates changed to preset: ") + presetName;
	}
	else
	{
		reply = FString("Failed to apply preset '") + presetName
			+ FString("'. Check the preset name and server logs for details.");
	}

	rcon_connection->SendMessageW(rcon_packet->Id, 0, &reply);
}

// ---------------------------------------------------------------------------
// ChangeRatesConsole
//
// In-game console (admin) command: changerates <preset_name>
//   Identical logic to the RCON handler but uses AShooterPlayerController
//   so the reply is sent back to the admin's chat window instead of the
//   RCON socket.
// ---------------------------------------------------------------------------
void ChangeRatesConsole(AShooterPlayerController* player, FString* message, bool /*written_to_console*/)
{
	FString reply;
	FString presetName;

	// message contains the full command line, e.g. "changerates weekend_rates"
	int32 spaceIdx = -1;
	if (message && message->FindChar(L' ', spaceIdx) && spaceIdx != INDEX_NONE)
	{
		std::string raw = message->Mid(spaceIdx + 1).ToString();

		// ltrim
		raw.erase(raw.begin(), std::find_if(raw.begin(), raw.end(),
			[](unsigned char c) { return !std::isspace(c); }));
		// rtrim
		raw.erase(std::find_if(raw.rbegin(), raw.rend(),
			[](unsigned char c) { return !std::isspace(c); }).base(), raw.end());

		presetName = FString(raw.c_str());
	}

	if (presetName.IsEmpty())
	{
		reply = "Usage: changerates <preset_name>\nAvailable presets:";

		if (CousinCustomRates::config.contains("RatePresets"))
		{
			for (auto& [key, val] : CousinCustomRates::config["RatePresets"].items())
			{
				reply += FString("\n  - ");
				reply += FString(key.c_str());
			}
		}

		AsaApi::GetApiUtils().SendServerMessage(player, FLinearColor(1.0f, 0.9f, 0.0f, 1.0f),
			"{}", reply.ToString());
		return;
	}

	Log::GetLog()->info("ChangeRatesConsole: received request to apply preset '{}'.",
		presetName.ToString());

	if (ApplyRates(presetName))
		reply = FString("Rates changed to preset: ") + presetName;
	else
		reply = FString("Failed to apply preset '") + presetName
			+ FString("'. Check the preset name and server logs for details.");

	AsaApi::GetApiUtils().SendServerMessage(player, FLinearColor(1.0f, 0.9f, 0.0f, 1.0f),
		"{}", reply.ToString());
}

// ---------------------------------------------------------------------------
// AddOrRemoveCommands
//   Registers or removes the 'changerates' RCON and console commands.
// ---------------------------------------------------------------------------
void AddOrRemoveCommands(bool addCmd = true)
{
	if (addCmd)
	{
		AsaApi::GetCommands().AddRconCommand("changerates", &ChangeRatesRcon);
		AsaApi::GetCommands().AddConsoleCommand("changerates", &ChangeRatesConsole);
		Log::GetLog()->info("Command 'changerates' registered (RCON + console).");
	}
	else
	{
		AsaApi::GetCommands().RemoveRconCommand("changerates");
		AsaApi::GetCommands().RemoveConsoleCommand("changerates");
	}
}
