#include "dock.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>

static int checks = 0;
static void check(bool value, const char *message) {
	++checks;
	if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
int main(int argc, char **argv) {
	QApplication app(argc, argv);
	QTemporaryDir temp;
	check(temp.isValid(), "temporary profile");
	const auto file = temp.path() + "/profile/settings.json";
	auto entries = defaults();
	QString language = "ru";
	check(entries.size() == 2 && !entries[0].enabled && !entries[1].enabled, "no unsolicited autostart");
	check(loadConfig(file, entries, language), "new settings");
	entries[0].path = "D:/Portable/RewardDesk.exe";
	entries[0].automatic = false;
	entries[0].enabled = true;
	check(saveConfig(file, entries, "uk"), "atomic save");
	auto loaded = defaults();
	check(loadConfig(file, loaded, language) && language == "uk", "load language");
	check(loaded[0].enabled && !loaded[0].automatic && loaded[0].path == entries[0].path, "manual path retained");
	check(!loaded[1].enabled, "disabled state retained");
	QFile corrupt(file); check(corrupt.open(QIODevice::WriteOnly), "corrupt fixture"); corrupt.write("{bad"); corrupt.close();
	check(!loadConfig(file, loaded, language) && loaded[0].enabled, "corruption never silently resets settings");
	LaunchDock broken(temp.path() + "/profile", "ru");
	check(!broken.findChild<QCheckBox *>("reward-desk-enabled")->isEnabled(), "corrupt profile disables controls");
	check(corrupt.open(QIODevice::ReadOnly) && corrupt.readAll() == "{bad", "corrupt file preserved"); corrupt.close();
	check(!validExecutable("cmd.exe") && !validExecutable("https://example.com/a.exe") && !validExecutable("\\\\server\\app.exe"), "non-local paths rejected");
	const auto exe = QCoreApplication::applicationDirPath() + "/launch-fixture.exe";
	check(validExecutable(exe), "PE accepted");
	check(!validExecutable(exe, "RewardDesk.exe"), "wrong product filename rejected");
	check(processState(exe) == ProcessState::Absent, "absent fixture");
	check(startApp(AppEntry{"test", "test", "no.exe", "", temp.path() + "/no.exe"}) != "running", "missing executable not launched");
	for (const auto locale : {"ru", "uk", "en"}) {
		LaunchDock dock(temp.path() + "/ui", locale, true);
		check(!dock.text("auto").isEmpty() && dock.text("auto") != "auto", "localized labels");
		check(dock.findChild<QLineEdit *>("reward-desk-path")->isReadOnly(), "path cannot inject arguments");
		dock.resize(320, 460); dock.show(); app.processEvents();
		for (auto *button : dock.findChildren<QPushButton *>()) check(button->geometry().right() <= button->parentWidget()->width(), "narrow dock controls fit");
		dock.beginSession(); dock.beginSession(); dock.stop();
	}
	// Optional real launch uses only the hidden test fixture, never a user's application.
	if (qEnvironmentVariableIsSet("LAUNCH_DESK_PROCESS_TEST")) {
		check(sameDesktopUser(), "desktop identity");
		AppEntry fixture{"launch-test", "Fixture", "launch-fixture.exe", "", exe};
		check(startApp(fixture) == "running", "Explorer launch confirmed");
		check(startApp(fixture) == "running", "duplicate launch skipped");
		const auto other = temp.path() + "/launch-fixture.exe";
		check(QFile::copy(exe, other), "alternate fixture");
		check(processState(other) == ProcessState::DifferentCopy, "other copy detected");
		QElapsedTimer elapsed; elapsed.start();
		while (processState(exe) != ProcessState::Absent && elapsed.elapsed() < 12000) QThread::msleep(100);
		check(processState(exe) == ProcessState::Absent, "fixture exited normally");
	}
	std::cout << checks << " checks passed\n";
	return 0;
}
