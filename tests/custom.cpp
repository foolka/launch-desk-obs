#include "dock.hpp"
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDialog>
#include <QMenu>
#include <QUuid>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QThread>
#include <QEventLoop>
#include <iostream>

static int count = 0;
static void check(bool ok, const char *message) {
	++count; if (!ok) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
static QByteArray read(const QString &file) { QFile input(file); check(input.open(QIODevice::ReadOnly), "read settings"); return input.readAll(); }
static void flush() { QApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
static AppEntry custom(const QString &exe, const QString &name) {
	AppEntry entry; entry.id = "custom-" + QUuid::createUuid().toString(QUuid::WithoutBraces); entry.name = name;
	entry.path = exe; entry.exe = QFileInfo(exe).fileName(); entry.custom = true; entry.automatic = false;
	return entry;
}
static void action(LaunchDock &dock, const QString &mode) {
	dock.findChild<QToolButton *>("set-options")->click(); flush();
	const auto menus = dock.findChildren<QMenu *>(); check(!menus.isEmpty(), "preset menu opens");
	auto *menu = menus.last(); auto *item = menu->findChild<QAction *>(mode); check(item, "preset action exists");
	item->trigger(); menu->close(); flush();
}
static void savePreset(LaunchDock &dock, const QString &name) {
	auto *dialog = dock.findChild<QDialog *>("preset-editor"); check(dialog, "preset editor exists");
	dialog->findChild<QLineEdit *>("preset-name")->setText(name); dialog->findChild<QPushButton *>("preset-save")->click(); flush();
}

// Exercise real dialogs and disk transitions without launching the user's programs.
int customChecks(const QString &exe) {
	QTemporaryDir temp; check(temp.isValid(), "custom temporary folder");
	const auto file = temp.path() + "/settings.json";
	auto first = custom(exe, "Звук и камера"); first.arguments = "--name \"Тест пробелов\" & literal"; first.workingDirectory = temp.path();
	check(validCustomEntry(first) && launchArguments(first) == first.arguments, "custom arguments are exact and contain no Desk flags");
	check(launchDirectory(first) == QDir::toNativeSeparators(temp.path()), "custom working directory");
	auto invalid = first; invalid.automatic = true; check(!validCustomEntry(invalid), "no auto discovery for custom programs");
	invalid = first; invalid.inTray = true; check(!validCustomEntry(invalid), "no undocumented tray flag for custom programs");
	invalid = first; invalid.path = "https://example.com/a.exe"; check(!validCustomEntry(invalid), "remote custom path rejected");
	invalid = first; invalid.arguments = QString("--bad") + QChar(0); check(!validCustomEntry(invalid), "NUL rejected");
	auto picked = custom(exe, ""); check(selectProgram(exe, picked) && !picked.name.isEmpty(), "EXE picker accepts local app");
	{
		using Microsoft::WRL::ComPtr;
		const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		ComPtr<IShellLinkW> link; ComPtr<IPersistFile> persist;
		check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))), "create test shortcut");
		link->SetPath(reinterpret_cast<LPCWSTR>(exe.utf16())); link->SetArguments(L"--name \"test space\"");
		link->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(temp.path().utf16()));
		const auto shortcut = temp.path() + "/Своя программа.lnk";
		check(SUCCEEDED(link.As(&persist)) && SUCCEEDED(persist->Save(reinterpret_cast<LPCWSTR>(shortcut.utf16()), TRUE)), "save test shortcut");
		check(selectProgram(shortcut, picked) && picked.arguments == "--name \"test space\"" && QFileInfo(picked.path) == QFileInfo(exe), "shortcut target and arguments imported without executing");
		persist.Reset(); link.Reset(); if (SUCCEEDED(initialized)) CoUninitialize();
	}
	LaunchLibrary library{{{"default", "Обычный эфир", defaults()}, {QUuid::createUuid().toString(QUuid::WithoutBraces), "VR", defaults()}}, "default", "ru"};
	library.presets[0].apps.append(first); library.presets[1].apps[0].enabled = true;
	library.presets[0].apps[0].path = "D:/Мои программы/RewardDesk.exe"; library.presets[0].apps[0].automatic = false;
	check(saveLibrary(file, library), "save complete library");
	LaunchLibrary loaded; check(loadLibrary(file, loaded) && loaded.presets.size() == 2 && loaded.presets[0].apps[4].arguments == first.arguments && loaded.presets[1].apps[0].enabled, "all sets survive reload");
	const auto original = read(file); check(loadLibrary(file, loaded) && read(file) == original, "read never changes bytes");
	loaded.activeId = loaded.presets[1].id; check(saveLibrary(file, loaded), "switch active set");
	check(read(file + ".previous.bak") == original, "previous valid file backed up");
	auto entries = defaults(); QString lang; check(loadConfig(file, entries, lang) && entries[0].enabled, "selected set loaded");
	auto broken = loaded; broken.activeId = "missing"; check(!saveLibrary(file, broken), "missing selection rejected");
	broken = loaded; broken.presets[1].id = "default"; check(!saveLibrary(file, broken), "duplicate set id rejected");
	broken = loaded; broken.presets[0].apps.append(first); check(!saveLibrary(file, broken), "duplicate custom id rejected");
	const auto preserved = read(file);
	QFile corrupt(temp.path() + "/future.json"); check(corrupt.open(QIODevice::WriteOnly), "open future fixture"); corrupt.write("{\"schema\":6}"); corrupt.close();
	check(!saveLibrary(corrupt.fileName(), library) && read(corrupt.fileName()) == "{\"schema\":6}", "future file never overwritten");
	check(read(file) == preserved, "failed validations preserve current file");
	{
		const auto oldFile = temp.path() + "/old/settings.json";
		QDir().mkpath(QFileInfo(oldFile).absolutePath());
		const auto document = QJsonDocument::fromJson(original).object();
		auto apps = document["profiles"].toArray().first().toObject()["apps"].toArray(); apps.removeLast();
		const auto oldBytes = QJsonDocument(QJsonObject{{"schema", 4}, {"language", "ru"}, {"apps", apps}}).toJson();
		QFile old(oldFile); check(old.open(QIODevice::WriteOnly) && old.write(oldBytes) == oldBytes.size(), "write 0.3 profile"); old.close();
		LaunchLibrary migrated; check(loadLibrary(oldFile, migrated) && migrated.presets.size() == 1 && read(oldFile) == oldBytes, "0.3 profile read without rewrite");
		check(saveLibrary(oldFile, migrated), "migrate 0.3 profile");
		const auto backup = oldFile + "." + QCryptographicHash::hash(oldBytes, QCryptographicHash::Sha256).toHex().left(16) + ".bak";
		check(read(backup) == oldBytes, "immutable pre-migration backup");
		const auto migratedBytes = read(oldFile);
		QDir().mkdir(oldFile + ".previous.bak");
		check(!saveLibrary(oldFile, migrated) && read(oldFile) == migratedBytes && read(backup) == oldBytes, "failed backup write cannot replace profile");
		QFile renamed(temp.path() + "/renamed-settings.json"); check(QFile::copy(file, renamed.fileName()), "copy renamed profile fixture");
		check(loadLibrary(renamed.fileName(), migrated) && migrated.presets.size() == 2, "explicit renamed file retains all sets");
		const auto portable = temp.path() + "/portable/config/launch-desk-obs/settings.json";
		check(saveLibrary(portable, library) && read(file) == preserved, "separate portable profile cannot overwrite installed profile");
	}
	{
		LaunchDock dock(temp.path(), "ru"); dock.setAttribute(Qt::WA_DontShowOnScreen); dock.resize(340, 580); dock.show(); flush();
		auto *sets = dock.findChild<QComboBox *>("startup-sets"); check(sets && sets->count() == 2 && sets->currentText() == "VR", "active set restored in UI");
		sets->setCurrentIndex(0); flush();
		check(dock.findChild<QPushButton *>(first.id + "-edit") && dock.findChild<QCheckBox *>(first.id + "-tray")->isHidden(), "custom controls without unsupported tray option");
		dock.findChild<QPushButton *>("add-app")->click(); flush();
		auto *editor = dock.findChild<QDialog *>("app-editor"); check(editor, "add dialog opens");
		for (auto *field : editor->findChildren<QLineEdit *>()) check(field->geometry().right() < field->parentWidget()->width(), "editor fields fit");
		const auto screenshots = qEnvironmentVariable("LAUNCH_DESK_QA_IMAGES");
		if (!screenshots.isEmpty()) { QDir().mkpath(screenshots); check(editor->grab().save(screenshots + "/app-editor.png"), "editor screenshot"); }
		editor->findChild<QPushButton *>("app-save")->click(); check(!editor->findChild<QLabel *>("app-error")->isHidden(), "empty app cannot be saved");
		editor->findChild<QLineEdit *>("app-name")->setText("Второе приложение"); editor->findChild<QLineEdit *>("app-program")->setText(exe);
		editor->findChild<QLineEdit *>("app-arguments")->setText("--silent"); editor->findChild<QPushButton *>("app-save")->click(); flush();
		check(loadLibrary(file, loaded) && loaded.presets[0].apps.size() == 6 && !loaded.presets[0].apps.last().enabled, "custom add persists disabled without launching");
		const auto secondId = loaded.presets[0].apps.last().id;
		action(dock, "clonePreset"); savePreset(dock, "Копия эфира");
		check(loadLibrary(file, loaded) && loaded.presets.size() == 3 && loaded.activeId == loaded.presets.last().id && loaded.presets.last().apps.size() == 6, "clone retains complete app settings");
		dock.findChild<QPushButton *>(first.id + "-remove")->click(); flush();
		auto *confirm = dock.findChild<QDialog *>("confirm-remove"); check(confirm, "remove asks confirmation"); confirm->reject(); flush();
		check(loadLibrary(file, loaded) && loaded.presets.last().apps.size() == 6, "cancel preserves entry");
		dock.findChild<QPushButton *>(first.id + "-remove")->click(); flush();
		dock.findChild<QDialog *>("confirm-remove")->findChild<QPushButton *>("confirm-delete")->click(); flush();
		dock.findChild<QCheckBox *>(secondId + "-enabled")->setChecked(true);
		check(loadLibrary(file, loaded) && loaded.presets.last().apps.size() == 5 && loaded.presets.last().apps.last().enabled && loaded.presets[0].apps.size() == 6 && !loaded.presets[0].apps.last().enabled, "remove/reindex targets correct entry and clone is independent");
		action(dock, "renamePreset"); savePreset(dock, "Игровой эфир");
		check(sets->currentText() == "Игровой эфир", "rename reflected");
		if (!screenshots.isEmpty()) check(dock.grab().save(screenshots + "/custom-set.png"), "custom set screenshot");
		action(dock, "newPreset"); savePreset(dock, "Пустой набор");
		check(loadLibrary(file, loaded) && loaded.presets.last().apps.size() == 4 && !loaded.presets.last().apps[0].enabled, "new set is opt-in");
		action(dock, "deletePreset"); dock.findChild<QDialog *>("confirm-remove")->findChild<QPushButton *>("confirm-delete")->click(); flush();
		check(loadLibrary(file, loaded) && loaded.presets.size() == 3 && loaded.activeId == "default", "delete set selects remaining set without deleting apps");
		dock.stop();
	}
	const auto release = [](const QString &tag, const QString &url) { return QJsonDocument(QJsonObject{{"tag_name", tag}, {"html_url", url}, {"draft", false}, {"prerelease", false}}).toJson(); };
	const auto link = QString("https://github.com/foolka/launch-desk-obs/releases/tag/");
	check(parseRelease(release("v0.10.0", link + "v0.10.0"), "0.9.0").state == "updateAvailable", "semantic version comparison");
	check(parseRelease(release("v0.3.0", link + "v0.3.0"), "0.4.0").state == "upToDate", "newer local build never downgraded");
	check(parseRelease(release("v0.4.0", link + "v0.4.0"), "0.4.0").state == "upToDate", "equal version");
	check(parseRelease(release("v0.5.0", "https://example.com/download.exe"), "0.4.0").state == "updateFailed", "foreign release link rejected");
	check(parseRelease(release("v0.5.0-beta", link + "v0.5.0-beta"), "0.4.0").state == "updateFailed", "prerelease tag rejected");
	check(parseRelease("bad json", "0.4.0").state == "updateFailed", "invalid response rejected");
	check(parseRelease(QByteArray(1048577, 'x'), "0.4.0").state == "updateFailed", "oversized response rejected");
	if (qEnvironmentVariableIsSet("LAUNCH_DESK_PROCESS_TEST")) {
		auto fixture = first;
		const auto report = temp.path() + "/launch-report.txt";
		fixture.arguments = "--report \"" + QDir::toNativeSeparators(report) + "\" \"Тест пробелов\" & literal";
		check(startApp(fixture) == "running", "custom executable starts through Explorer");
		QElapsedTimer elapsed; elapsed.start();
		while (!QFileInfo::exists(report) && elapsed.elapsed() < 5000) QThread::msleep(50);
		const auto bytes = read(report);
		const auto output = QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData()), bytes.size() / 2);
		check(output == QDir::toNativeSeparators(temp.path()) + "\nТест пробелов\n&\nliteral\n", "real launch keeps Unicode arguments and working directory without shell interpretation");
		check(startApp(fixture) == "running", "custom duplicate skipped");
		while (processState(exe) != ProcessState::Absent && elapsed.elapsed() < 12000) QThread::msleep(100);
		check(processState(exe) == ProcessState::Absent, "custom fixture exits normally");
		QTemporaryDir pending;
		auto enabled = fixture; enabled.enabled = true;
		LaunchLibrary queued{{{"default", "One", defaults()}, {QUuid::createUuid().toString(QUuid::WithoutBraces), "Two", defaults()}}, "default", "en"};
		queued.presets[0].apps.append(enabled);
		check(saveLibrary(pending.path() + "/settings.json", queued), "pending startup fixture");
		LaunchDock dock(pending.path(), "en"); dock.setAttribute(Qt::WA_DontShowOnScreen);
		dock.beginSession(); dock.beginSession(); dock.findChild<QComboBox *>("startup-sets")->setCurrentIndex(1);
		QEventLoop wait; QTimer::singleShot(5000, &wait, &QEventLoop::quit); wait.exec();
		check(processState(exe) == ProcessState::Absent, "switching cancels pending startup without starting new set");
		dock.stop();
	}
	if (qEnvironmentVariableIsSet("LAUNCH_DESK_NETWORK_TEST")) {
		UpdateChecker checker; QEventLoop wait; UpdateResult result;
		checker.check(LAUNCH_DESK_VERSION, [&](UpdateResult response) { result = response; wait.quit(); });
		QTimer::singleShot(20000, &wait, &QEventLoop::quit); wait.exec();
		check(result.state == "upToDate" || result.state == "updateAvailable", "live HTTPS release check");
		std::cout << "GitHub release check: " << result.state.toStdString() << '\n';
		bool staleCallback = false;
		checker.check("0.0.0", [&](UpdateResult) { staleCallback = true; }); checker.cancel(); flush();
		check(!staleCallback, "cancelled update never calls disposed UI");
		result = {};
		checker.check("0.0.0", [&](UpdateResult response) { result = response; wait.quit(); });
		QTimer::singleShot(20000, &wait, &QEventLoop::quit); wait.exec();
		check(result.state == "updateAvailable" && !result.url.isEmpty() && !staleCallback, "live newer-release detection after cancellation");
	}
	return count;
}
