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
#include <vector>

using Microsoft::WRL::ComPtr;

// Fixed product identities keep automatic discovery away from arbitrary executables.
QVector<AppEntry> defaults() {
	return {
		{"reward-desk", "Reward Desk", "RewardDesk.exe", "{C65EBA28-42E6-4D4C-A350-E4080559F1B2}_is1"},
		{"shoutout-desk", "Shoutout Desk", "ShoutoutDesk.exe", "{AFC7E0E6-8D63-42B3-8A41-76C28D90E29B}_is1"}
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

// Parse into a temporary model; malformed or future settings never reset saved choices.
bool loadConfig(const QString &file, QVector<AppEntry> &apps, QString &language) {
	QFile input(file);
	if (!input.exists()) return true;
	if (!input.open(QIODevice::ReadOnly) || input.size() > 65536) return false;
	QJsonParseError error;
	const auto doc = QJsonDocument::fromJson(input.readAll(), &error);
	const auto object = doc.object();
	if (error.error != QJsonParseError::NoError || object.value("schema").toInt() != 1 || !object.value("apps").isArray()) return false;
	auto next = defaults();
	const auto records = object.value("apps").toArray();
	if (records.size() != next.size()) return false;
	for (auto &app : next) {
		int matches = 0;
		for (const auto value : records) {
			const auto row = value.toObject();
			if (row.value("id").toString() != app.id) continue;
			if (!row.value("enabled").isBool() || !row.value("automatic").isBool() || !row.value("path").isString()) return false;
			app.path = row.value("path").toString();
			app.enabled = row.value("enabled").toBool();
			app.automatic = row.value("automatic").toBool();
			++matches;
		}
		if (matches != 1) return false;
	}
	const auto lang = object.value("language").toString();
	if (!QStringList{"ru", "uk", "en"}.contains(lang)) return false;
	apps = next;
	language = lang;
	return true;
}

// Atomic replacement preserves the previous settings when a write fails.
bool saveConfig(const QString &file, const QVector<AppEntry> &apps, const QString &language) {
	if (!QDir().mkpath(QFileInfo(file).absolutePath())) return false;
	QJsonArray rows;
	for (const auto &app : apps) rows.append(QJsonObject{{"id", app.id}, {"path", app.path}, {"enabled", app.enabled}, {"automatic", app.automatic}});
	QSaveFile output(file);
	const auto bytes = QJsonDocument(QJsonObject{{"schema", 1}, {"language", language}, {"apps", rows}}).toJson();
	return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
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
static HRESULT launchThroughDesktop(const QString &path, const QString &arguments) {
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
	const auto cwd = QDir::toNativeSeparators(QFileInfo(path).absolutePath());
	directory.bstrVal = SysAllocString(reinterpret_cast<LPCWSTR>(cwd.utf16()));
	verb.bstrVal = SysAllocString(L"open");
	show.vt = VT_I4;
	show.lVal = SW_SHOWNOACTIVATE;
	hr = shell->ShellExecute(file, args, directory, verb, show);
	SysFreeString(file);
	VariantClear(&args); VariantClear(&directory); VariantClear(&verb);
	return hr;
}

// A per-product mutex serializes launches across multiple OBS instances.
QString startApp(const AppEntry &entry) {
	if (!sameDesktopUser()) return "identity";
	if (!validExecutable(entry.path, entry.exe)) return "missing";
	const auto mutexName = QString("Local\\FermionaPlay.LaunchDesk.") + entry.id;
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
			const auto hr = launchThroughDesktop(entry.path, entry.id == "shoutout-desk" ? "--start-tray" : "");
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
