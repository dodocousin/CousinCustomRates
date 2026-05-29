#pragma once

#include "json.hpp"
#include "Requests.h"

namespace CousinCustomRates
{
	// Parsed config.json contents
	inline nlohmann::json config;

	// Name of the currently active rate preset (e.g. "weekend_rates")
	inline std::string lastPreset;

	// HTTP request helper (used for Discord webhook)
	static API::Requests& req = API::Requests::Get();
}
