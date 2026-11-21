#include "launcher.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shldisp.h>
#include <exdisp.h>
#include <wrl/client.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QSet>
#include <QUuid>
#include <vector>

using Microsoft::WRL::ComPtr;

// Fixed product identities keep automatic discovery away from arbitrary executables.
QVector<AppEntry> defaults() {
	return {
		{"reward-desk", "Reward Desk", "RewardDesk.exe", "{C65EBA28-42E6-4D4C-A350-E4080559F1B2}_is1"},
		{"shoutout-desk", "Shoutout Desk", "ShoutoutDesk.exe", "{AFC7E0E6-8D63-42B3-8A41-76C28D90E29B}_is1"},
		{"dudos-desk", "Dudos Desk", "DudosDesk.exe", "{5431F79E-B94F-4474-8727-21426CCB251A}_is1"},
		{"command-desk", "Command Desk", "CommandDesk.exe", "{F08D55A4-6584-46E5-B8EB-2069DDA9E886}_is1"}
	};
}

// Reject links, commands, UNC paths and non-executables before any launch attempt.
bool validExecutable(const QString &path, const QString &expectedName) {
	const QFileInfo info(path);
	if (!QDir::isAbsolutePath(path) || path.startsWith("\\\\") || path.startsWith("//") ||
		path.contains('"') || !info.isFile() || info.suffix().compare("exe", Qt::CaseInsensitive) != 0 ||
		(!expectedName.isEmpty() && info.fileName().compare(expectedName, Qt::CaseInsensitive) != 0)) return false;
	DWORD type = 0;
	return GetBinaryTypeW(reinterpret_cast<LPCWSTR>(info.absoluteFilePath().utf16()), &type) &&
		(type == SCS_64BIT_BINARY || type == SCS_32BIT_BINARY);
}

// Refuse another Windows identity rather than discovering or opening its profile.
bool sameDesktopUser() {
	DWORD pid = 0;
	GetWindowThreadProcessId(GetShellWindow(), &pid);
	HANDLE shell = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!shell) return false;
	auto user = [](HANDLE process) {
		std::vector<BYTE> result;
		HANDLE token = nullptr;
		if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return result;
		DWORD size = 0;
		GetTokenInformation(token, TokenUser, nullptr, 0, &size);
		result.resize(size);
		if (!GetTokenInformation(token, TokenUser, result.data(), size, &size)) result.clear();
		CloseHandle(token);
		return result;
	};
	const auto current = user(GetCurrentProcess()), desktop = user(shell);
	CloseHandle(shell);
	return !current.empty() && !desktop.empty() && EqualSid(
		reinterpret_cast<const TOKEN_USER *>(current.data())->User.Sid,
		reinterpret_cast<const TOKEN_USER *>(desktop.data())->User.Sid);
}

// Read only the registered installation and the supported per-user install directory.
QString detectInstalled(const AppEntry &app) {
	if (app.custom) return {};
	if (!sameDesktopUser()) return {};
	const QString sub = "\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + app.uninstallKey;
	for (const QString hive : {QString("HKEY_CURRENT_USER"), QString("HKEY_LOCAL_MACHINE")}) {
		QSettings registry(hive + sub, QSettings::NativeFormat);
		const auto root = registry.value("InstallLocation").toString();
		if (root.isEmpty()) continue;
		const auto path = QDir(root).filePath(app.exe);
		if (validExecutable(path, app.exe)) return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
	}
	const auto local = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	const auto path = local + "/Programs/" + app.name + "/" + app.exe;
	return validExecutable(path, app.exe) ? QDir::toNativeSeparators(path) : QString();
}

// Custom records keep explicit local paths, never a shell command in the path field.
bool validCustomEntry(const AppEntry &entry) {
	const auto clean = [](const QString &value, int limit) {
		return value.size() <= limit && !value.contains(QChar(0)) && !value.contains('\n') && !value.contains('\r');
	};
	const auto local = [](const QString &path) {
		return QDir::isAbsolutePath(path) && !path.startsWith("\\\\") && !path.startsWith("//") && !path.contains('"');
	};
	return entry.custom && entry.id.startsWith("custom-") && !QUuid(entry.id.mid(7)).isNull() &&
		!entry.automatic && !entry.inTray && !entry.name.trimmed().isEmpty() && clean(entry.name, 120) &&
		clean(entry.path, 32767) && local(entry.path) && QFileInfo(entry.path).suffix().compare("exe", Qt::CaseInsensitive) == 0 &&
		clean(entry.arguments, 8192) && clean(entry.workingDirectory, 32767) &&
		(entry.workingDirectory.isEmpty() || local(entry.workingDirectory));
}

// Read shortcut fields without resolving network targets or invoking the shortcut.
bool selectProgram(const QString &file, AppEntry &entry) {
	auto selected = entry;
	selected.path = QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath());
	selected.arguments.clear(); selected.workingDirectory.clear();
	if (QFileInfo(file).suffix().compare("lnk", Qt::CaseInsensitive) == 0) {
		if (!QDir::isAbsolutePath(file) || file.startsWith("\\\\") || file.startsWith("//")) return false;
		const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;
		bool ok = false;
		{
			ComPtr<IShellLinkW> link; ComPtr<IPersistFile> persist;
			if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) &&
				SUCCEEDED(link.As(&persist)) && SUCCEEDED(persist->Load(reinterpret_cast<LPCWSTR>(file.utf16()), STGM_READ))) {
				wchar_t path[32768]{}, arguments[32768]{}, directory[32768]{};
				ok = SUCCEEDED(link->GetPath(path, 32768, nullptr, SLGP_RAWPATH)) &&
					SUCCEEDED(link->GetArguments(arguments, 32768)) && SUCCEEDED(link->GetWorkingDirectory(directory, 32768));
				selected.path = QString::fromWCharArray(path);
				selected.arguments = QString::fromWCharArray(arguments);
				selected.workingDirectory = QString::fromWCharArray(directory);
			}
		}
		if (SUCCEEDED(init)) CoUninitialize();
		if (!ok) return false;
	}
	if (!validExecutable(selected.path)) return false;
	selected.exe = QFileInfo(selected.path).fileName();
	if (selected.name.trimmed().isEmpty()) selected.name = QFileInfo(file).completeBaseName();
	entry = selected;
	return true;
}

// Parse into a temporary model; malformed or future settings never reset saved choices.
static bool parseApps(const QJsonObject &object, QVector<AppEntry> &apps, QString &language) {
	const int schema = object.value("schema").toInt();
	if (schema < 1 || schema > 5 || !object.value("apps").isArray()) return false;
	auto next = defaults();
	const auto records = object.value("apps").toArray();
	const int expectedCount = schema < 3 ? 2 : schema == 3 ? 3 : next.size();
	if ((schema < 5 && records.size() != expectedCount) || (schema == 5 && (records.size() < next.size() || records.size() > 104))) return false;
	for (auto &app : next) {
		// New products stay opt-out; loading never rewrites a user's older configuration.
		if (schema < 3 && app.id == "dudos-desk") continue;
		if (schema < 4 && app.id == "command-desk") continue;
		int matches = 0;
		for (const auto value : records) {
			const auto row = value.toObject();
			if (row.value("id").toString() != app.id) continue;
			if (!row.value("enabled").isBool() || !row.value("automatic").isBool() || !row.value("path").isString()) return false;
			app.path = row.value("path").toString();
			app.enabled = row.value("enabled").toBool();
			app.automatic = row.value("automatic").toBool();
			if (schema >= 2 && !row.value("inTray").isBool()) return false;
			app.inTray = schema == 1 ? app.id == "shoutout-desk" : row.value("inTray").toBool();
			++matches;
		}
		if (matches != 1) return false;
	}
	QSet<QString> ids;
	for (const auto &app : next) ids.insert(app.id);
	if (schema == 5) for (const auto value : records) {
		const auto row = value.toObject();
		const auto id = row.value("id").toString();
		if (!id.startsWith("custom-")) {
			if (!ids.contains(id) || row.value("custom").toBool()) return false;
			continue;
		}
		if (ids.contains(id) || !row.value("custom").isBool() || !row.value("custom").toBool()) return false;
		for (const auto key : {"name", "path", "arguments", "workingDirectory"}) if (!row.value(key).isString()) return false;
		for (const auto key : {"enabled", "automatic", "inTray"}) if (!row.value(key).isBool()) return false;
		AppEntry entry;
		entry.id = id; entry.custom = true; entry.name = row.value("name").toString(); entry.path = row.value("path").toString();
		entry.exe = QFileInfo(entry.path).fileName(); entry.arguments = row.value("arguments").toString();
		entry.workingDirectory = row.value("workingDirectory").toString();
		entry.enabled = row.value("enabled").toBool(); entry.automatic = row.value("automatic").toBool(); entry.inTray = row.value("inTray").toBool();
		if (!validCustomEntry(entry)) return false;
		ids.insert(id); next.append(entry);
	}
	const auto lang = object.value("language").toString();
	if (!QStringList{"ru", "uk", "en"}.contains(lang)) return false;
	apps = next;
	language = lang;
	return true;
}

// Atomic replacement preserves the previous settings when a write fails.
static QJsonArray appRecords(const QVector<AppEntry> &apps) {
	QJsonArray rows;
	for (const auto &app : apps) {
		QJsonObject row{{"id", app.id}, {"path", app.path}, {"enabled", app.enabled}, {"automatic", app.automatic}, {"inTray", app.inTray}};
		if (app.custom) {
			row.insert("custom", true); row.insert("name", app.name); row.insert("arguments", app.arguments); row.insert("workingDirectory", app.workingDirectory);
		}
		rows.append(row);
	}
	return rows;
}

// All presets share one atomic document; a selection cannot point at a missing file.
static bool parseLibrary(const QJsonObject &object, LaunchLibrary &library) {
	LaunchLibrary next;
	next.language = object.value("language").toString();
	const int schema = object.value("schema").toInt();
	if (schema >= 1 && schema <= 4) {
		QVector<AppEntry> apps;
		if (!parseApps(object, apps, next.language)) return false;
		next.activeId = "default";
		next.presets.append({"default", next.language == "ru" ? "Основной" : next.language == "uk" ? "Основний" : "Default", apps});
	} else if (schema == 5) {
		if (!object.value("profiles").isArray() || !object.value("activeProfile").isString()) return false;
		const auto profiles = object.value("profiles").toArray();
		if (profiles.isEmpty() || profiles.size() > 20) return false;
		QSet<QString> ids;
		for (const auto value : profiles) {
			const auto record = value.toObject();
			const auto id = record.value("id").toString();
			const auto name = record.value("name").toString();
			if ((id != "default" && QUuid(id).isNull()) || ids.contains(id) || name.trimmed().isEmpty() || name.size() > 80 || name.contains(QChar(0)) || name.contains('\n') || name.contains('\r')) return false;
			QVector<AppEntry> apps; QString language;
			if (!parseApps(QJsonObject{{"schema", 5}, {"language", next.language}, {"apps", record.value("apps")}}, apps, language)) return false;
			ids.insert(id); next.presets.append({id, name, apps});
		}
		next.activeId = object.value("activeProfile").toString();
		if (!ids.contains(next.activeId)) return false;
	} else return false;
	library = next;
	return true;
}

// Failed or future formats leave the caller's complete library untouched.
bool loadLibrary(const QString &file, LaunchLibrary &library) {
	QFile input(file);
	if (!input.exists()) return true;
	if (!input.open(QIODevice::ReadOnly) || input.size() > 1048576) return false;
	QJsonParseError error;
	const auto document = QJsonDocument::fromJson(input.readAll(), &error);
	return error.error == QJsonParseError::NoError && parseLibrary(document.object(), library);
}

// Atomic writes and versioned backups preserve settings across upgrades and failed writes.
bool saveLibrary(const QString &file, const LaunchLibrary &library) {
	QJsonArray profiles;
	for (const auto &preset : library.presets) profiles.append(QJsonObject{{"id", preset.id}, {"name", preset.name}, {"apps", appRecords(preset.apps)}});
	const QJsonObject document{{"schema", 5}, {"language", library.language}, {"activeProfile", library.activeId}, {"profiles", profiles}};
	LaunchLibrary validated;
	if (!parseLibrary(document, validated)) return false;
	const auto bytes = QJsonDocument(document).toJson();
	if (bytes.size() > 1048576 || !QDir().mkpath(QFileInfo(file).absolutePath())) return false;
	// Keep the previous valid file before any schema migration; never overwrite damaged data.
	if (QFileInfo::exists(file)) {
		LaunchLibrary previous;
		if (!loadLibrary(file, previous)) return false;
		QFile input(file);
		if (!input.open(QIODevice::ReadOnly)) return false;
		const auto old = input.readAll();
		const bool migration = QJsonDocument::fromJson(old).object().value("schema").toInt() < 5;
		{
			const auto backup = migration ? file + "." + QCryptographicHash::hash(old, QCryptographicHash::Sha256).toHex().left(16) + ".bak" : file + ".previous.bak";
			if (!migration || !QFileInfo::exists(backup)) {
				QSaveFile copy(backup);
				if (!copy.open(QIODevice::WriteOnly) || copy.write(old) != old.size() || !copy.commit()) return false;
			} else {
				QFile copy(backup);
				if (!copy.open(QIODevice::ReadOnly) || copy.readAll() != old) return false;
			}
		}
	}
	QSaveFile output(file);
	return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}

// Single-set callers retain the selected preset rather than discarding the other sets.
bool loadConfig(const QString &file, QVector<AppEntry> &apps, QString &language) {
	LaunchLibrary library;
	if (!loadLibrary(file, library)) return false;
	if (library.presets.isEmpty()) return true;
	for (const auto &preset : library.presets) if (preset.id == library.activeId) apps = preset.apps;
	language = library.language;
	return true;
}

// Compatibility helper for callers that only edit the currently selected set.
bool saveConfig(const QString &file, const QVector<AppEntry> &apps, const QString &language) {
	LaunchLibrary library;
	if (!loadLibrary(file, library)) return false;
	if (library.presets.isEmpty()) { library.activeId = "default"; library.presets.append({"default", "Default", apps}); }
	for (auto &preset : library.presets) if (preset.id == library.activeId) preset.apps = apps;
	library.language = language;
	return saveLibrary(file, library);
}

// Query only processes in this desktop session; never terminate or attach to an app.
ProcessState processState(const QString &path) {
	const auto canonical = QFileInfo(path).canonicalFilePath();
	const auto name = QFileInfo(path).fileName();
	DWORD session = 0;
	if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return ProcessState::Unknown;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return ProcessState::Unknown;
	PROCESSENTRY32W process{};
	process.dwSize = sizeof(process);
	auto result = ProcessState::Absent;
	if (!Process32FirstW(snapshot, &process)) result = ProcessState::Unknown;
	else do {
		if (QString::fromWCharArray(process.szExeFile).compare(name, Qt::CaseInsensitive) != 0) continue;
		DWORD otherSession = 0;
		if (!ProcessIdToSessionId(process.th32ProcessID, &otherSession)) { result = ProcessState::Unknown; break; }
		if (otherSession != session) continue;
		HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.th32ProcessID);
		wchar_t buffer[32768];
		DWORD size = 32768;
		if (!handle || !QueryFullProcessImageNameW(handle, 0, buffer, &size)) {
			if (handle) CloseHandle(handle);
			result = ProcessState::Unknown;
			break;
		}
		CloseHandle(handle);
		const auto other = QFileInfo(QString::fromWCharArray(buffer, size)).canonicalFilePath();
		result = !canonical.isEmpty() && canonical.compare(other, Qt::CaseInsensitive) == 0 ? ProcessState::Running : ProcessState::DifferentCopy;
		break;
	} while (Process32NextW(snapshot, &process));
	CloseHandle(snapshot);
	return result;
}

// Explorer owns the non-elevated desktop identity, even when OBS is elevated.
static HRESULT launchThroughDesktop(const QString &path, const QString &arguments, const QString &cwd) {
	ComPtr<IShellWindows> windows;
	HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows));
	if (FAILED(hr)) return hr;
	VARIANT empty{};
	long hwnd = 0;
	ComPtr<IDispatch> desktop;
	hr = windows->FindWindowSW(&empty, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &desktop);
	if (FAILED(hr) || !desktop) return E_FAIL;
	ComPtr<IServiceProvider> services;
	ComPtr<IShellBrowser> browser;
	ComPtr<IShellView> view;
	ComPtr<IDispatch> background, application;
	ComPtr<IShellFolderViewDual> folder;
	ComPtr<IShellDispatch2> shell;
	if (FAILED(desktop.As(&services)) || FAILED(services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) ||
		FAILED(browser->QueryActiveShellView(&view)) || FAILED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background))) ||
		FAILED(background.As(&folder)) || FAILED(folder->get_Application(&application)) || FAILED(application.As(&shell))) return E_FAIL;
	BSTR file = SysAllocString(reinterpret_cast<LPCWSTR>(path.utf16()));
	VARIANT args{}, directory{}, verb{}, show{};
	args.vt = directory.vt = verb.vt = VT_BSTR;
	args.bstrVal = SysAllocString(reinterpret_cast<LPCWSTR>(arguments.utf16()));
	directory.bstrVal = SysAllocString(reinterpret_cast<LPCWSTR>(cwd.utf16()));
	verb.bstrVal = SysAllocString(L"open");
	show.vt = VT_I4;
	show.lVal = SW_SHOWNOACTIVATE;
	hr = shell->ShellExecute(file, args, directory, verb, show);
	SysFreeString(file);
	VariantClear(&args); VariantClear(&directory); VariantClear(&verb);
	return hr;
}

// Explicit arguments override only this launch, never the application's saved preference.
QString launchArguments(const AppEntry &entry) {
	return entry.custom ? entry.arguments : entry.inTray ? "--start-tray" : "--start-window";
}

// An empty working directory follows the chosen executable's folder.
QString launchDirectory(const AppEntry &entry) {
	return QDir::toNativeSeparators(entry.custom && !entry.workingDirectory.isEmpty() ? entry.workingDirectory : QFileInfo(entry.path).absolutePath());
}

// A per-product mutex serializes launches across multiple OBS instances.
QString startApp(const AppEntry &entry) {
	if (!sameDesktopUser()) return "identity";
	if (!validExecutable(entry.path, entry.exe)) return "missing";
	if (entry.custom && !validCustomEntry(entry)) return "invalidCustom";
	if (!QFileInfo(launchDirectory(entry)).isDir()) return "directoryMissing";
	const auto identity = entry.custom ? QString::fromLatin1(QCryptographicHash::hash(QFileInfo(entry.path).canonicalFilePath().toLower().toUtf8(), QCryptographicHash::Sha256).toHex()) : entry.id;
	const auto mutexName = QString("Local\\FermionaPlay.LaunchDesk.") + identity;
	HANDLE mutex = CreateMutexW(nullptr, FALSE, reinterpret_cast<LPCWSTR>(mutexName.utf16()));
	if (!mutex) return "failed";
	const DWORD wait = WaitForSingleObject(mutex, 0);
	if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) { CloseHandle(mutex); return "busy"; }
	QString result;
	switch (processState(entry.path)) {
	case ProcessState::Running: result = "running"; break;
	case ProcessState::DifferentCopy: result = "different"; break;
	case ProcessState::Unknown: result = "unknown"; break;
	case ProcessState::Absent: {
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) result = "failed";
		else {
			const auto hr = launchThroughDesktop(entry.path, launchArguments(entry), launchDirectory(entry));
			result = SUCCEEDED(hr) ? "starting" : "failed";
			if (SUCCEEDED(hr)) {
				for (int attempt = 0; attempt < 50; ++attempt) {
					if (processState(entry.path) == ProcessState::Running) { result = "running"; break; }
					Sleep(100);
				}
				if (result == "starting") result = "unconfirmed";
			}
		}
		if (SUCCEEDED(initialized)) CoUninitialize();
		break;
	}
	}
	ReleaseMutex(mutex);
	CloseHandle(mutex);
	return result;
}
