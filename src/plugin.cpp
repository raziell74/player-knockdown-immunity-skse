#include "PCH.h"

#include "Settings/Settings.h"

SKSE_EXPORT constinit SKSE::PluginVersionData SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName("PlayerKnockdownImmunity");
	v.AuthorName("Raziell74"sv);
	v.PluginVersion({ 0, 1, 0, 0 });
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	v.CompatibleVersions({
		SKSE::RUNTIME_SSE_1_5_97,
		SKSE::RUNTIME_SSE_1_6_1170,
		SKSE::RUNTIME_SSE_1_7_99,
	});
	return v;
}();

SKSE_EXPORT bool SKSEPlugin_Query(SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->name = SKSEPlugin_Version.GetPluginName().data();
	pluginInfo->version = SKSEPlugin_Version.GetPluginVersion().pack();
	return true;
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// Debug CRT: Module::_instance is not constinit. If any Relocation resolved
	// during static init, get() marks the singleton initialized and then the
	// constructor zeros _base — later ID lookups jump to a raw offset (crash).
	REL::Module::reset();
	SKSE::Init(a_skse);
	Settings::Load();

	const auto* plugin = SKSE::PluginVersionData::GetSingleton();
	SKSE::log::info("{} v{} loaded", plugin->GetPluginName(), plugin->GetPluginVersion().string());

	return true;
}
