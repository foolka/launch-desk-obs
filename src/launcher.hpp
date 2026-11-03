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
};

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
