#pragma once

#include <spdlog/spdlog.h>

namespace Settings
{
	struct Config
	{
		bool                      enabled{ true };
		bool                      combatOnly{ true };
		spdlog::level::level_enum level{ spdlog::level::info };
	};

	[[nodiscard]] const Config& Get();
	void                        Load();
}
