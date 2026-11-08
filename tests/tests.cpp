#include "dock.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QScrollArea>
#include <QScrollBar>
#include <iostream>

static int checks = 0;
static void check(bool value, const char *message) {
	++checks;
	if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
int main(int argc, char **argv) {
	QApplication app(argc, argv);
	app.setStyle("Fusion");
	app.setFont(QFont("Segoe UI", 10));
	QTemporaryDir temp;
	check(temp.isValid(), "temporary profile");
	const auto file = temp.path() + "/profile/settings.json";
	auto entries = defaults();
	QString language = "ru";
	check(entries.size() == 3 && !entries[0].enabled && !entries[1].enabled && !entries[2].enabled, "no unsolicited autostart");
	check(entries[2].id == "dudos-desk" && entries[2].name == "Dudos Desk" && entries[2].exe == "DudosDesk.exe" && entries[2].uninstallKey == "{5431F79E-B94F-4474-8727-21426CCB251A}_is1", "Dudos installer identity");
	check(loadConfig(file, entries, language), "new settings");
	check(!entries[0].inTray && !entries[1].inTray && !entries[2].inTray, "new profiles default to normal windows");
	check(launchArguments(entries[2]) == "--start-window", "Dudos normal window argument");
	entries[2].inTray = true;
	check(launchArguments(entries[2]) == "--start-tray", "Dudos tray argument");
	entries[2].inTray = false;
	check(launchArguments(entries[0]) == "--start-window", "normal window argument");
	entries[0].inTray = true;
	check(launchArguments(entries[0]) == "--start-tray", "tray argument");
	entries[0].path = "D:/Portable/RewardDesk.exe";
	entries[0].automatic = false;
	entries[0].enabled = true;
	check(saveConfig(file, entries, "uk"), "atomic save");
	auto loaded = defaults();
	check(loadConfig(file, loaded, language) && language == "uk", "load language");
	check(loaded[0].enabled && !loaded[0].automatic && loaded[0].path == entries[0].path, "manual path retained");
	check(!loaded[1].enabled, "disabled state retained");
	check(loaded[0].inTray && !loaded[1].inTray, "independent tray modes retained");
	QFile legacy(file);
	check(legacy.open(QIODevice::ReadOnly), "read migration fixture");
	auto legacyObject = QJsonDocument::fromJson(legacy.readAll()).object(); legacy.close();
	check(legacyObject["schema"].toInt() == 3, "writes current schema");
	const auto currentObject = legacyObject;
	legacyObject["schema"] = 1;
	QJsonArray oldRows;
	for (auto value : legacyObject["apps"].toArray()) { auto row = value.toObject(); if (row["id"] == "dudos-desk") continue; row.remove("inTray"); oldRows.append(row); }
	legacyObject["apps"] = oldRows;
	const auto legacyBytes = QJsonDocument(legacyObject).toJson();
	check(legacy.open(QIODevice::WriteOnly) && legacy.write(legacyBytes) == legacyBytes.size(), "write version 1 fixture"); legacy.close();
	check(loadConfig(file, loaded, language), "old profile accepted");
	check(!loaded[0].inTray && loaded[1].inTray && loaded[0].enabled && !loaded[0].automatic && loaded[0].path == entries[0].path && language == "uk", "old profile preserves modes paths switches and language");
	check(!loaded[2].enabled && loaded[2].automatic && !loaded[2].inTray && loaded[2].path.isEmpty(), "schema 1 adds only disabled Dudos defaults");
	check(legacy.open(QIODevice::ReadOnly) && legacy.readAll() == legacyBytes, "loading never rewrites old profile"); legacy.close();
	{
		LaunchDock migrated(temp.path() + "/profile", "en");
		auto *rewardTray = migrated.findChild<QCheckBox *>("reward-desk-tray");
		auto *shoutoutTray = migrated.findChild<QCheckBox *>("shoutout-desk-tray");
		check(rewardTray && shoutoutTray && !rewardTray->isChecked() && shoutoutTray->isChecked(), "legacy tray controls match previous behavior");
		rewardTray->setChecked(true); shoutoutTray->setChecked(false);
		check(loadConfig(file, loaded, language) && loaded[0].inTray && !loaded[1].inTray && loaded[0].enabled && loaded[0].path == entries[0].path, "checkboxes persist independently without changing autostart or path");
	}
	auto v2 = currentObject;
	v2["schema"] = 2;
	auto v2Rows = v2["apps"].toArray(); v2Rows.removeLast(); v2["apps"] = v2Rows;
	const auto v2Bytes = QJsonDocument(v2).toJson();
	check(legacy.open(QIODevice::WriteOnly) && legacy.write(v2Bytes) == v2Bytes.size(), "write version 2 fixture"); legacy.close();
	check(loadConfig(file, loaded, language) && language == "uk" && loaded[0].enabled && loaded[0].inTray && !loaded[0].automatic && loaded[0].path == entries[0].path && !loaded[1].enabled && !loaded[1].inTray, "schema 2 keeps both existing apps unchanged");
	check(!loaded[2].enabled && loaded[2].automatic && !loaded[2].inTray && loaded[2].path.isEmpty(), "schema 2 adds disabled Dudos defaults");
	{
		LaunchDock migrated(temp.path() + "/profile", "en");
		check(legacy.open(QIODevice::ReadOnly) && legacy.readAll() == v2Bytes, "opening dock preserves old file bytes"); legacy.close();
		auto *enabled = migrated.findChild<QCheckBox *>("dudos-desk-enabled");
		auto *tray = migrated.findChild<QCheckBox *>("dudos-desk-tray");
		check(enabled && tray && !enabled->isChecked() && !tray->isChecked(), "Dudos controls start off after upgrade");
		enabled->setChecked(true); tray->setChecked(true);
		check(loadConfig(file, loaded, language) && loaded[2].enabled && loaded[2].inTray && loaded[0].enabled && loaded[0].inTray && loaded[0].path == entries[0].path && !loaded[1].enabled && language == "uk", "Dudos switches persist without changing existing choices");
	}
	loaded[2].path = "D:/Portable/DudosDesk.exe"; loaded[2].automatic = false;
	check(saveConfig(file, loaded, language) && loadConfig(file, loaded, language) && loaded[2].path == "D:/Portable/DudosDesk.exe" && !loaded[2].automatic && loaded[2].inTray && loaded[2].enabled, "Dudos portable path and modes survive reload");
	for (const auto failure : {"future", "missing", "duplicate", "unknown"}) {
		auto invalid = currentObject;
		auto rows = invalid["apps"].toArray();
		if (QString(failure) == "future") invalid["schema"] = 4;
		if (QString(failure) == "missing") rows.removeLast();
		if (QString(failure) == "duplicate") rows[2] = rows[0];
		if (QString(failure) == "unknown") { auto row = rows[2].toObject(); row["id"] = "unknown-app"; rows[2] = row; }
		invalid["apps"] = rows;
		check(legacy.open(QIODevice::WriteOnly), "write rejected fixture"); legacy.write(QJsonDocument(invalid).toJson()); legacy.close();
		check(!loadConfig(file, loaded, language) && loaded[2].path == "D:/Portable/DudosDesk.exe" && loaded[2].enabled, "malformed or future settings preserve current model");
	}
	legacyObject["schema"] = 2;
	check(legacy.open(QIODevice::WriteOnly), "write invalid mode fixture"); legacy.write(QJsonDocument(legacyObject).toJson()); legacy.close();
	check(!loadConfig(file, loaded, language), "new schema without tray fields fails closed");
	QFile corrupt(file); check(corrupt.open(QIODevice::WriteOnly), "corrupt fixture"); corrupt.write("{bad"); corrupt.close();
	check(!loadConfig(file, loaded, language) && loaded[0].enabled, "corruption never silently resets settings");
	LaunchDock broken(temp.path() + "/profile", "ru");
	check(!broken.findChild<QCheckBox *>("reward-desk-enabled")->isEnabled(), "corrupt profile disables controls");
	check(!broken.findChild<QCheckBox *>("reward-desk-tray")->isEnabled(), "corrupt profile disables tray mode");
	check(!broken.findChild<QCheckBox *>("dudos-desk-enabled")->isEnabled() && !broken.findChild<QCheckBox *>("dudos-desk-tray")->isEnabled(), "corrupt profile disables Dudos controls");
	check(corrupt.open(QIODevice::ReadOnly) && corrupt.readAll() == "{bad", "corrupt file preserved"); corrupt.close();
	check(!validExecutable("cmd.exe") && !validExecutable("https://example.com/a.exe") && !validExecutable("\\\\server\\app.exe"), "non-local paths rejected");
	const auto exe = QCoreApplication::applicationDirPath() + "/launch-fixture.exe";
	check(validExecutable(exe), "PE accepted");
	check(!validExecutable(exe, "RewardDesk.exe"), "wrong product filename rejected");
	check(!validExecutable(exe, "DudosDesk.exe"), "wrong Dudos filename rejected");
	check(QFile::copy(exe, temp.path() + "/DudosDesk.exe") && validExecutable(temp.path() + "/DudosDesk.exe", "DudosDesk.exe"), "portable Dudos PE accepted without running it");
	check(processState(exe) == ProcessState::Absent, "absent fixture");
	check(startApp(AppEntry{"test", "test", "no.exe", "", temp.path() + "/no.exe"}) != "running", "missing executable not launched");
	for (const auto locale : {"ru", "uk", "en"}) {
		LaunchDock dock(temp.path() + "/ui", locale, true);
		dock.setAttribute(Qt::WA_DontShowOnScreen);
		check(!dock.text("auto").isEmpty() && dock.text("auto") != "auto" && dock.text("tray") != "tray", "localized labels");
		check(dock.findChild<QLineEdit *>("reward-desk-path")->isReadOnly(), "path cannot inject arguments");
		check(dock.findChild<QLineEdit *>("dudos-desk-path")->isReadOnly() && dock.findChild<QCheckBox *>("dudos-desk-tray")->text() == dock.text("tray"), "Dudos path and localized tray controls");
		dock.resize(320, 460); dock.show(); app.processEvents();
		check(dock.height() == 460 && dock.width() == 320, "dock does not force OBS larger");
		auto *scroll = dock.findChild<QScrollArea *>("apps-scroll");
		check(scroll && scroll->verticalScrollBar()->maximum() > 0, "short dock scrolls vertically");
		if (scroll->horizontalScrollBar()->maximum() != 0) std::cerr << "Overflow in " << locale << ": " << scroll->horizontalScrollBar()->maximum() << " px, content minimum " << scroll->widget()->minimumSizeHint().width() << '\n';
		check(scroll->horizontalScrollBar()->maximum() == 0, "narrow dock has no horizontal overflow");
		scroll->ensureWidgetVisible(dock.findChild<QCheckBox *>("dudos-desk-tray")); app.processEvents();
		check(scroll->verticalScrollBar()->value() > 0, "third app reachable by scrolling");
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
