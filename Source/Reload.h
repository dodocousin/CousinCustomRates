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
// ReloadConfigConsole
//   In-game console (admin) command: CousinCustomRates.Reload
//   Same logic as the RCON handler but replies to the admin's in-game
//   console/chat instead of the RCON socket.
// ---------------------------------------------------------------------------
void ReloadConfigConsole(AShooterPlayerController* player, FString* /*message*/, bool /*written_to_console*/)
{
	FString reply;

	try
	{
		Reload();
		reply = "CousinCustomRates: config.json reloaded successfully.";
	}
	catch (const std::exception& error)
	{
		reply = FString("CousinCustomRates: failed to reload config. ERROR: ")
			+ FString(error.what());
	}

	AsaApi::GetApiUtils().SendServerMessage(player, FLinearColor(1.0f, 0.9f, 0.0f, 1.0f),
		"{}", reply.ToString());
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
		AsaApi::GetCommands().AddConsoleCommand(reloadCmd, &ReloadConfigConsole);
		Log::GetLog()->info("Command '{}' registered (RCON + console).", reloadCmd.ToString());
	}
	else
	{
		AsaApi::GetCommands().RemoveRconCommand(reloadCmd);
		AsaApi::GetCommands().RemoveConsoleCommand(reloadCmd);
	}
}
