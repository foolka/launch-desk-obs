#include "dock.hpp"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QPalette>

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	app.setStyle("Fusion");
	QPalette palette;
	palette.setColor(QPalette::Window, QColor("#25272f"));
	palette.setColor(QPalette::WindowText, QColor("#f2f2f4"));
	palette.setColor(QPalette::Base, QColor("#1d1e24"));
	palette.setColor(QPalette::Text, QColor("#f2f2f4"));
	palette.setColor(QPalette::Button, QColor("#3b3e49"));
	palette.setColor(QPalette::ButtonText, QColor("#ffffff"));
	palette.setColor(QPalette::Highlight, QColor("#37927d"));
	palette.setColor(QPalette::HighlightedText, Qt::white);
	app.setPalette(palette);
	app.setFont(QFont("Segoe UI", 10));
	const auto args = app.arguments();
	LaunchDock dock("", args.value(1, "en"), true);
	dock.setAttribute(Qt::WA_DontShowOnScreen);
	dock.setWindowTitle("Launch Desk OBS");
	dock.resize(440, 460);
	dock.show();
	QTimer::singleShot(100, &app, [&] {
		const auto path = args.value(2);
		QDir().mkpath(QFileInfo(path).absolutePath());
		const bool saved = dock.grab().save(path);
		app.exit(saved ? 0 : 1);
	});
	return app.exec();
}
