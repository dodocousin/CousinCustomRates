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
// AddOrRemoveCommands
//   Registers or removes the 'changerates' RCON command.
// ---------------------------------------------------------------------------
void AddOrRemoveCommands(bool addCmd = true)
{
	if (addCmd)
	{
		AsaApi::GetCommands().AddRconCommand("changerates", &ChangeRatesRcon);
		Log::GetLog()->info("Command 'changerates' registered.");
	}
	else
	{
		AsaApi::GetCommands().RemoveRconCommand("changerates");
	}
}
