#include <windows.h>
#include "launcher.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QTimer>
#include <QCheckBox>
#include <iostream>

// Test the actual release DLL against only the small public frontend API it consumes.
int main(int argc, char **argv) {
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid() || !app.arguments().contains("--launch-desk-test")) return 1;
	qputenv("LAUNCH_DESK_TEST_PROFILE", temp.path().toUtf8());
	const auto bin = QCoreApplication::applicationDirPath();
	QLibrary frontend(bin + "/obs-frontend-api.dll"), plugin(bin + "/launch-desk-obs.dll");
	if (!frontend.load() || !plugin.load()) { std::cerr << "DLL load failed\n"; return 1; }
	const auto load = reinterpret_cast<bool(*)()>(plugin.resolve("obs_module_load"));
	const auto post = reinterpret_cast<void(*)()>(plugin.resolve("obs_module_post_load"));
	const auto unload = reinterpret_cast<void(*)()>(plugin.resolve("obs_module_unload"));
	const auto event = reinterpret_cast<void(*)(int)>(frontend.resolve("test_event"));
	const auto widget = reinterpret_cast<QWidget*(*)()>(frontend.resolve("test_dock"));
	if (!load || !post || !unload || !event || !widget || !load()) return 1;
	post();
	if (!widget() || widget()->findChild<QCheckBox *>("reward-desk-enabled")->isChecked()) return 1;
	event(26); event(26); event(13); event(15); event(36);
	unload();
	if (widget() || QFile::exists(temp.path() + "/settings.json")) return 1;
	std::cout << "Release DLL: exports, dock registration, default-off startup, repeated events, shutdown and no unsolicited writes passed\n";
	return 0;
}
