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

class LaunchDock : public QWidget {
public:
	LaunchDock(const QString &profile, const QString &locale, bool preview = false);
	~LaunchDock() override;
	void beginSession();
	void stop();
	QString text(const QString &key) const;
private:
	struct Row {
		QCheckBox *enabled;
		QLabel *mode, *status;
		QLineEdit *path;
		QPushButton *browse, *detect, *launch;
		QString state;
		bool busy = false;
	};
	QVector<AppEntry> apps = defaults();
	QVector<Row> rows;
	QVector<QThread *> workers;
	QString file, language;
	QComboBox *languages;
	QLabel *notice;
	QTimer poll;
	bool valid = true, demo = false, begun = false, stopped = false;
	void refresh();
	void launch(int index);
	void persist();
	void status();
};
