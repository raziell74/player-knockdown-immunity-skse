#pragma once

#include <spdlog/spdlog.h>

namespace Settings
{
	struct Config
	{
		spdlog::level::level_enum level{ spdlog::level::info };
	};

	[[nodiscard]] const Config& Get();
	void                        Load();
}
