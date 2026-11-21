#pragma once
#include <QString>
#include <QVector>
#include <QJsonObject>

struct AppEntry {
	QString id, name, exe, uninstallKey;
	QString path;
	bool enabled = false;
	bool automatic = true;
	bool inTray = false;
	bool custom = false;
	QString arguments, workingDirectory;
};

struct LaunchPreset {
	QString id, name;
	QVector<AppEntry> apps;
};
struct LaunchLibrary {
	QVector<LaunchPreset> presets;
	QString activeId, language;
};

bool loadLibrary(const QString &file, LaunchLibrary &library);
bool saveLibrary(const QString &file, const LaunchLibrary &library);

QVector<AppEntry> defaults();
bool validExecutable(const QString &path, const QString &expectedName = {});
QString detectInstalled(const AppEntry &app);
bool sameDesktopUser();
bool loadConfig(const QString &file, QVector<AppEntry> &apps, QString &language);
bool saveConfig(const QString &file, const QVector<AppEntry> &apps, const QString &language);
enum class ProcessState { Absent, Running, DifferentCopy, Unknown };
ProcessState processState(const QString &path);
QString startApp(const AppEntry &entry);
QString launchArguments(const AppEntry &entry);
bool validCustomEntry(const AppEntry &entry);
bool selectProgram(const QString &file, AppEntry &entry);
QString launchDirectory(const AppEntry &entry);
