#pragma once

// ---------------------------------------------------------------------------
// Reload
//   Re-reads config.json without requiring a server restart.
//   The currently active preset is NOT re-applied automatically; use
//   'changerates <preset>' again if you changed multiplier values.
//
//   After reloading, the schedule timer is always stopped and then
//   conditionally restarted based on the new Schedule.Enabled value.
//   This ensures that:
//     - enabling the schedule in config takes effect immediately
//     - disabling the schedule in config stops the timer immediately
//     - a changed CheckIntervalSeconds takes effect on the next cycle
// ---------------------------------------------------------------------------
void Reload()
{
	ReadConfig();

	// Stop the existing timer (safe if it wasn't running), then re-evaluate
	// the new config to decide whether to restart it.
	SetTimers(false);
	SetTimers();
}

// ---------------------------------------------------------------------------
// ReloadConfigRcon
//   RCON command: CousinCustomRates.Reload
// ---------------------------------------------------------------------------
void ReloadConfigRcon(RCONClientConnection* rcon_connection, RCONPacket* rcon_packet, UWorld*)
{
	FString reply;

	try
	{
		Reload();
		reply = "CousinCustomRates: config.json reloaded successfully.";
	}
	catch (const std::exception& error)
	{
		// FString concatenation — avoids wide/narrow string mixing
		reply = FString("CousinCustomRates: failed to reload config. ERROR: ")
			+ FString(error.what());
	}

	rcon_connection->SendMessageW(rcon_packet->Id, 0, &reply);
}

// ---------------------------------------------------------------------------
// AddReloadCommands / Remove
// ---------------------------------------------------------------------------
void AddReloadCommands(bool addCmd = true)
{
	const FString reloadCmd = std::string(PROJECT_NAME + std::string(".Reload")).c_str();

	if (addCmd)
	{
		AsaApi::GetCommands().AddRconCommand(reloadCmd, &ReloadConfigRcon);
		Log::GetLog()->info("Command '{}' registered.", reloadCmd.ToString());
	}
	else
	{
		AsaApi::GetCommands().RemoveRconCommand(reloadCmd);
	}
}
