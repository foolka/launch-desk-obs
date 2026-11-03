#include <windows.h>
#include <cstdint>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QPointer>
#include "dock.hpp"

static QPointer<LaunchDock> dock;
static bool registered = false;
static QString locale;
static constexpr const char *DockId = "launch-desk-obs";
// Stable public OBS 32 frontend enum values (frontend/api/obs-frontend-api.h).
static constexpr int FinishedLoading = 26, Exit = 17, ScriptingShutdown = 36;

template<typename T> static T api(const char *name) {
	auto handle = GetModuleHandleW(L"obs-frontend-api.dll");
	return handle ? reinterpret_cast<T>(GetProcAddress(handle, name)) : nullptr;
}

static void event(int value, void *) {
	if (!dock) return;
	if (value == FinishedLoading) dock->beginSession();
	if (value == Exit || value == ScriptingShutdown) dock->stop();
}

extern "C" __declspec(dllexport) void obs_module_set_pointer(void *) {}
extern "C" __declspec(dllexport) uint32_t obs_module_ver() { return 32u << 24; }
extern "C" __declspec(dllexport) const char *obs_module_name() { return "Launch Desk OBS"; }
extern "C" __declspec(dllexport) const char *obs_module_description() { return "Start Reward Desk and Shoutout Desk with OBS."; }
extern "C" __declspec(dllexport) const char *obs_module_author() { return "FermionaPlay"; }
extern "C" __declspec(dllexport) void obs_module_set_locale(const char *value) { locale = QString::fromUtf8(value); }
extern "C" __declspec(dllexport) bool obs_module_load() {
	return api<bool(*)(const char *, const char *, void *)>("obs_frontend_add_dock_by_id") &&
		api<void(*)(void(*)(int, void *), void *)>("obs_frontend_add_event_callback");
}

extern "C" __declspec(dllexport) void obs_module_post_load() {
	if (dock) return;
	auto profile = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/Launch Desk OBS";
	const auto root = QDir(QCoreApplication::applicationDirPath() + "/../..").absolutePath();
	const auto args = QCoreApplication::arguments();
	if (QFileInfo::exists(root + "/portable_mode") || QFileInfo::exists(root + "/portable_mode.txt") ||
		args.contains("--portable") || args.contains("-p")) profile = root + "/config/launch-desk-obs";
	const auto testProfile = qEnvironmentVariable("LAUNCH_DESK_TEST_PROFILE");
	if (args.contains("--launch-desk-test") && QDir::isAbsolutePath(testProfile)) profile = testProfile;
	dock = new LaunchDock(profile, locale.isEmpty() ? QLocale::system().name() : locale);
	const auto add = api<bool(*)(const char *, const char *, void *)>("obs_frontend_add_dock_by_id");
	registered = add && add(DockId, "Launch Desk", dock.data());
	if (!registered) { delete dock.data(); return; }
	api<void(*)(void(*)(int, void *), void *)>("obs_frontend_add_event_callback")(event, nullptr);
}

extern "C" __declspec(dllexport) void obs_module_unload() {
	const auto removeEvent = api<void(*)(void(*)(int, void *), void *)>("obs_frontend_remove_event_callback");
	if (removeEvent) removeEvent(event, nullptr);
	if (dock) dock->stop();
	const auto remove = api<void(*)(const char *)>("obs_frontend_remove_dock");
	if (dock && registered && remove) remove(DockId);
	if (dock) delete dock.data();
	dock = nullptr; registered = false;
}
