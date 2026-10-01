#include "PCH.h"

#include "Settings/Settings.h"

#include <SimpleIni.h>

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	Settings::Config g_config{};

	[[nodiscard]] std::filesystem::path IniPath()
	{
		std::vector<wchar_t> buf(260);
		const auto           len = REX::W32::GetModuleFileNameW(
            REX::W32::GetCurrentModule(),
            buf.data(),
            static_cast<std::uint32_t>(buf.size()));
		if (!len || len >= buf.size()) {
			return {};
		}

		std::filesystem::path path(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(len));
		path.replace_extension(".ini");
		return path;
	}

	[[nodiscard]] std::string ToLower(std::string a_s)
	{
		for (char& c : a_s) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return a_s;
	}

	[[nodiscard]] spdlog::level::level_enum ParseLevel(std::string_view a_name, bool& a_valid)
	{
		const auto lowered = ToLower(std::string(a_name));
		const auto level = spdlog::level::from_str(lowered);
		a_valid = !(level == spdlog::level::off && lowered != "off");
		return a_valid ? level : spdlog::level::info;
	}

	[[nodiscard]] bool ParseBool(std::string_view a_value, bool a_default, bool& a_valid)
	{
		const auto lowered = ToLower(std::string(a_value));
		if (lowered == "true" || lowered == "1" || lowered == "yes") {
			a_valid = true;
			return true;
		}
		if (lowered == "false" || lowered == "0" || lowered == "no") {
			a_valid = true;
			return false;
		}

		a_valid = false;
		return a_default;
	}

	void ApplyLevel(spdlog::level::level_enum a_level)
	{
		spdlog::set_level(a_level);
		spdlog::flush_on(a_level);
		if (auto logger = spdlog::default_logger()) {
			logger->set_level(a_level);
			logger->flush_on(a_level);
		}
	}
}

namespace Settings
{
	const Config& Get()
	{
		return g_config;
	}

	void Load()
	{
		g_config = Config{};

		const auto path = IniPath();
		if (path.empty() || !std::filesystem::exists(path)) {
			ApplyLevel(g_config.level);
			SKSE::log::info(
				"INI not found at {}; using defaults (Enabled=true CombatOnly=true Level=info)",
				path.empty() ? "<unknown>" : path.string());
			return;
		}

		CSimpleIniA ini;
		ini.SetUnicode();
		const auto rc = ini.LoadFile(path.string().c_str());
		if (rc < 0) {
			ApplyLevel(g_config.level);
			SKSE::log::warn(
				"Failed to load INI {}; using defaults (Enabled=true CombatOnly=true Level=info)",
				path.string());
			return;
		}

		bool enabledValid = true;
		bool combatOnlyValid = true;
		bool levelValid = true;
		g_config.enabled = ParseBool(ini.GetValue("General", "Enabled", "true"), true, enabledValid);
		g_config.combatOnly = ParseBool(ini.GetValue("General", "CombatOnly", "true"), true, combatOnlyValid);
		g_config.level = ParseLevel(ini.GetValue("Logging", "Level", "info"), levelValid);

		ApplyLevel(g_config.level);

		if (!enabledValid) {
			SKSE::log::warn("Invalid General.Enabled in {}; using true", path.string());
		}
		if (!combatOnlyValid) {
			SKSE::log::warn("Invalid General.CombatOnly in {}; using true", path.string());
		}
		if (!levelValid) {
			SKSE::log::warn("Invalid Logging.Level in {}; using info", path.string());
		}

		SKSE::log::info(
			"Settings: file={} enabled={} combatOnly={} level={}",
			path.string(),
			g_config.enabled,
			g_config.combatOnly,
			spdlog::level::to_string_view(g_config.level));
	}
}
