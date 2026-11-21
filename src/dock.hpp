#pragma once
#include "launcher.hpp"
#include <QWidget>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QToolButton>
#include <functional>
#include "updates.hpp"

class LaunchDock : public QWidget {
public:
	LaunchDock(const QString &profile, const QString &locale, bool preview = false);
	~LaunchDock() override;
	void beginSession();
	void stop();
	QString text(const QString &key) const;
private:
	struct Row {
		QString id;
		QWidget *widget;
		QLabel *name;
		QCheckBox *enabled, *inTray;
		QLabel *mode, *status;
		QLineEdit *path;
		QPushButton *browse, *detect, *launch;
		QPushButton *edit, *remove;
		QString state;
		bool busy = false;
	};
	QVector<AppEntry> apps = defaults();
	QVector<Row> rows;
	QVector<QThread *> workers;
	LaunchLibrary library;
	QComboBox *presets;
	QToolButton *presetMenu;
	QPushButton *addApp, *updateButton, *releaseButton;
	QLabel *updateStatus;
	QVBoxLayout *sections;
	QWidget *content;
	UpdateChecker *updater;
	QTimer updateTimer;
	UpdateResult update;
	quint64 generation = 0;
	QString file, language;
	QComboBox *languages;
	QLabel *notice;
	QTimer poll;
	bool valid = true, demo = false, begun = false, stopped = false;
	void refresh();
	void launch(const QString &id);
	void persist();
	void status();
	int indexOf(const QString &id) const;
	void rebuildRows();
	void editApp(const QString &id = {});
	void removeApp(const QString &id);
	void editPreset(const QString &mode);
	void switchPreset(const QString &id);
	void confirm(const QString &title, const QString &message, std::function<void()> action);
	void checkUpdates();
};
