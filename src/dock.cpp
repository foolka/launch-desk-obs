#include "dock.hpp"
#include <QHBoxLayout>
#include <QFileDialog>
#include <QStyle>
#include <QFrame>
#include <QScrollArea>
#include <QFileInfo>
#include <QDir>
#include <QMap>
#include <QMenu>
#include <QSignalBlocker>
#include <QDesktopServices>
#include <QCoreApplication>

// Use OBS's native palette and keep the application list independently scrollable.
LaunchDock::LaunchDock(const QString &profile, const QString &locale, bool preview) :
	file(profile + "/settings.json"), language(locale.left(2)), demo(preview) {
	if (!QStringList{"ru", "uk", "en"}.contains(language)) language = "en";
	library = {{{"default", text("defaultPreset"), defaults()}}, "default", language};
	valid = demo || loadLibrary(file, library); language = library.language;
	for (const auto &preset : library.presets) if (preset.id == library.activeId) apps = preset.apps;
	for (auto &entry : apps) {
		if (entry.automatic) entry.path = demo ? "C:\\Users\\Streamer\\AppData\\Local\\Programs\\" + entry.name + "\\" + entry.exe : detectInstalled(entry);
		if (demo) entry.enabled = true;
	}
	auto *layout = new QVBoxLayout(this); layout->setContentsMargins(12, 12, 12, 12); layout->setSpacing(10);
	setMinimumWidth(280);
	auto *heading = new QHBoxLayout;
	heading->addWidget(new QLabel("Launch Desk <small>" LAUNCH_DESK_VERSION "</small>"), 1);
	languages = new QComboBox; languages->setObjectName("language");
	languages->addItem("Русский", "ru"); languages->addItem("Українська", "uk"); languages->addItem("English", "en");
	languages->setCurrentIndex(languages->findData(language)); heading->addWidget(languages);
	updateButton = new QPushButton; updateButton->setObjectName("check-updates");
	updateButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload)); updateButton->setFixedSize(28, 28); heading->addWidget(updateButton);
	layout->addLayout(heading);
	auto *setRow = new QHBoxLayout;
	presets = new QComboBox; presets->setObjectName("startup-sets"); presets->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); setRow->addWidget(presets, 1);
	presetMenu = new QToolButton; presetMenu->setObjectName("set-options"); presetMenu->setText("…"); presetMenu->setFixedSize(30, 30); setRow->addWidget(presetMenu);
	layout->addLayout(setRow);
	auto *scroll = new QScrollArea; scroll->setObjectName("apps-scroll"); scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	content = new QWidget; sections = new QVBoxLayout(content); sections->setContentsMargins(0, 0, 4, 0); sections->setSpacing(12);
	scroll->setWidget(content); layout->addWidget(scroll, 1);
	addApp = new QPushButton; addApp->setObjectName("add-app"); addApp->setIcon(style()->standardIcon(QStyle::SP_FileIcon)); layout->addWidget(addApp);
	updateStatus = new QLabel; updateStatus->setWordWrap(true); layout->addWidget(updateStatus);
	releaseButton = new QPushButton; releaseButton->setObjectName("open-release"); releaseButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp)); layout->addWidget(releaseButton);
	notice = new QLabel; notice->setWordWrap(true); notice->setVisible(!valid); layout->addWidget(notice);
	updater = new UpdateChecker(this);
	connect(languages, &QComboBox::currentIndexChanged, this, [this] { language = languages->currentData().toString(); persist(); refresh(); });
	connect(presets, &QComboBox::currentIndexChanged, this, [this] { switchPreset(presets->currentData().toString()); });
	connect(presetMenu, &QToolButton::clicked, this, [this] {
		auto *menu = new QMenu(this); menu->setAttribute(Qt::WA_DeleteOnClose);
		menu->setAttribute(Qt::WA_DontShowOnScreen, testAttribute(Qt::WA_DontShowOnScreen));
		for (const auto mode : {"newPreset", "clonePreset", "renamePreset", "deletePreset"}) {
			auto *action = menu->addAction(text(mode)); action->setObjectName(mode);
			action->setEnabled(valid && (QString(mode) != "deletePreset" || library.presets.size() > 1));
			connect(action, &QAction::triggered, this, [this, mode] { editPreset(mode); });
		}
		menu->popup(presetMenu->mapToGlobal(QPoint(0, presetMenu->height())));
	});
	connect(addApp, &QPushButton::clicked, this, [this] { editApp(); });
	connect(updateButton, &QPushButton::clicked, this, [this] { checkUpdates(); });
	connect(releaseButton, &QPushButton::clicked, this, [this] { if (update.state == "updateAvailable") QDesktopServices::openUrl(QUrl(update.url)); });
	connect(&poll, &QTimer::timeout, this, [this] { status(); });
	connect(&updateTimer, &QTimer::timeout, this, [this] { checkUpdates(); });
	if (!demo) poll.start(5000);
	if (!demo && !qEnvironmentVariableIsSet("LAUNCH_DESK_OFFLINE_TEST") && !QCoreApplication::arguments().contains("--launch-desk-test")) {
		QTimer::singleShot(10000, this, [this] { checkUpdates(); }); updateTimer.start(6 * 60 * 60 * 1000);
	}
	rebuildRows(); refresh();
}

// Resolve IDs after edits/removals so delayed work cannot target a shifted row.
int LaunchDock::indexOf(const QString &id) const {
	for (int i = 0; i < apps.size(); ++i) if (apps[i].id == id) return i;
	return -1;
}

// Rebuild only for structural edits, retaining in-flight state by stable identity.
void LaunchDock::rebuildRows() {
	QMap<QString, Row> old; for (const auto &row : rows) old.insert(row.id, row);
	while (auto *item = sections->takeAt(0)) { if (auto *widget = item->widget()) { widget->setParent(nullptr); widget->deleteLater(); } delete item; }
	rows.clear();
	for (const auto &entry : apps) {
		const auto id = entry.id;
		Row row{}; row.id = id; row.widget = new QWidget(content);
		auto *section = new QVBoxLayout(row.widget); section->setContentsMargins(0, 0, 0, 0); section->setSpacing(7);
		auto *nameRow = new QHBoxLayout;
		row.name = new QLabel(entry.name); row.name->setTextFormat(Qt::PlainText); row.name->setWordWrap(true);
		auto font = row.name->font(); font.setBold(true); row.name->setFont(font); nameRow->addWidget(row.name, 1);
		row.edit = new QPushButton; row.edit->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView)); row.edit->setObjectName(id + "-edit");
		row.remove = new QPushButton; row.remove->setIcon(style()->standardIcon(QStyle::SP_TrashIcon)); row.remove->setObjectName(id + "-remove");
		for (auto *button : {row.edit, row.remove}) { button->setFixedSize(28, 28); button->setVisible(entry.custom); nameRow->addWidget(button); }
		section->addLayout(nameRow);
		row.enabled = new QCheckBox; row.enabled->setObjectName(id + "-enabled"); row.enabled->setChecked(entry.enabled); section->addWidget(row.enabled);
		row.inTray = new QCheckBox; row.inTray->setObjectName(id + "-tray"); row.inTray->setChecked(entry.inTray); row.inTray->setVisible(!entry.custom); section->addWidget(row.inTray);
		row.path = new QLineEdit(entry.path); row.path->setObjectName(id + "-path"); row.path->setReadOnly(true);
		auto *pathRow = new QHBoxLayout; pathRow->setSpacing(5); pathRow->addWidget(row.path, 1);
		row.browse = new QPushButton; row.browse->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
		row.detect = new QPushButton; row.detect->setIcon(style()->standardIcon(QStyle::SP_BrowserReload)); row.detect->setVisible(!entry.custom);
		for (auto *button : {row.browse, row.detect}) { button->setFixedSize(30, 30); pathRow->addWidget(button); }
		section->addLayout(pathRow);
		auto *bottom = new QHBoxLayout; row.mode = new QLabel; row.mode->setWordWrap(true); bottom->addWidget(row.mode, 1);
		row.launch = new QPushButton; row.launch->setObjectName(id + "-launch"); row.launch->setIcon(style()->standardIcon(QStyle::SP_MediaPlay)); bottom->addWidget(row.launch); section->addLayout(bottom);
		row.status = new QLabel; row.status->setWordWrap(true); row.status->setObjectName(id + "-status"); section->addWidget(row.status);
		row.state = old.contains(id) ? old[id].state : entry.path.isEmpty() ? "missing" : "ready"; row.busy = old.contains(id) && old[id].busy;
		rows.append(row); sections->addWidget(row.widget);
		auto *line = new QFrame; line->setFrameShape(QFrame::HLine); sections->addWidget(line);
		connect(row.enabled, &QCheckBox::toggled, this, [this, id](bool value) { const int i = indexOf(id); if (i >= 0) { apps[i].enabled = value; persist(); } });
		connect(row.inTray, &QCheckBox::toggled, this, [this, id](bool value) { const int i = indexOf(id); if (i >= 0) { apps[i].inTray = value; persist(); } });
		connect(row.edit, &QPushButton::clicked, this, [this, id] { editApp(id); });
		connect(row.remove, &QPushButton::clicked, this, [this, id] { removeApp(id); });
		connect(row.browse, &QPushButton::clicked, this, [this, id] {
			int i = indexOf(id); if (i < 0 || rows[i].busy) return;
			if (apps[i].custom) { editApp(id); return; }
			const auto selected = QFileDialog::getOpenFileName(this, text("browse"), QFileInfo(apps[i].path).absolutePath(), apps[i].exe + " (" + apps[i].exe + ")");
			i = indexOf(id); if (selected.isEmpty() || i < 0) return;
			if (!validExecutable(selected, apps[i].exe)) { notice->setText(text("invalid").arg(apps[i].exe)); notice->show(); return; }
			apps[i].path = QDir::toNativeSeparators(selected); apps[i].automatic = false; rows[i].state = "ready"; persist(); refresh();
		});
		connect(row.detect, &QPushButton::clicked, this, [this, id] {
			const int i = indexOf(id); if (i < 0 || rows[i].busy) return;
			apps[i].path = detectInstalled(apps[i]); apps[i].automatic = true; rows[i].state = apps[i].path.isEmpty() ? "missing" : "ready"; persist(); refresh();
		});
		connect(row.launch, &QPushButton::clicked, this, [this, id] { launch(id); });
	}
	sections->addStretch(); refresh();
}

// Save every set in one transaction, and only after explicit edits.
void LaunchDock::persist() {
	if (demo || !valid) return;
	for (auto &preset : library.presets) if (preset.id == library.activeId) preset.apps = apps;
	library.language = language;
	if (!saveLibrary(file, library)) { valid = false; notice->setText(text("saveError")); notice->show(); refresh(); }
}

// Routine refreshes preserve controls and keyboard focus.
void LaunchDock::refresh() {
	languages->setAccessibleName(text("language")); languages->setEnabled(valid);
	{ const QSignalBlocker block(presets); presets->clear(); for (const auto &preset : library.presets) presets->addItem(preset.name, preset.id); presets->setCurrentIndex(presets->findData(library.activeId)); }
	presets->setAccessibleName(text("presets")); presets->setToolTip(text("presets")); presets->setEnabled(valid);
	presetMenu->setAccessibleName(text("presets")); presetMenu->setToolTip(text("presets")); presetMenu->setEnabled(valid);
	addApp->setText(text("addApp")); addApp->setEnabled(valid && apps.size() < 104);
	for (int i = 0; i < rows.size(); ++i) {
		auto &row = rows[i]; row.enabled->setText(text("auto")); row.enabled->setEnabled(valid);
		row.inTray->setText(text("tray")); row.inTray->setEnabled(valid && !row.busy);
		row.path->setText(apps[i].path); row.path->setCursorPosition(0); row.path->setToolTip(apps[i].path); row.path->setAccessibleName(apps[i].name + " EXE");
		row.mode->setText(text(apps[i].automatic ? "detected" : "manual"));
		for (const auto pair : {qMakePair(row.browse, "browse"), qMakePair(row.detect, "detect"), qMakePair(row.edit, "editApp"), qMakePair(row.remove, "removeApp")}) {
			pair.first->setToolTip(text(pair.second)); pair.first->setAccessibleName(text(pair.second)); pair.first->setEnabled(valid && !row.busy);
		}
		row.launch->setText(text("launch")); row.launch->setEnabled(valid && !demo && !stopped && !row.busy); row.status->setText(text(row.state));
	}
	updateButton->setToolTip(text("checkUpdates")); updateButton->setAccessibleName(text("checkUpdates")); updateButton->setEnabled(!demo && update.state != "checking");
	updateStatus->setText(update.state == "updateAvailable" ? text(update.state).arg(update.version) : text(update.state)); updateStatus->setVisible(!update.state.isEmpty());
	releaseButton->setText(text("openRelease")); releaseButton->setVisible(update.state == "updateAvailable");
	if (!valid && notice->text().isEmpty()) notice->setText(text("settingsError"));
}

// Polling reports process state without restarting applications.
void LaunchDock::status() {
	for (int i = 0; i < rows.size(); ++i) {
		if (rows[i].busy || apps[i].path.isEmpty()) continue;
		const auto state = processState(apps[i].path);
		if (state == ProcessState::Running) rows[i].state = "running";
		else if (state == ProcessState::DifferentCopy) rows[i].state = "different";
		else if (rows[i].state == "running") rows[i].state = "ready";
		rows[i].status->setText(text(rows[i].state));
	}
}

// Worker results belong to the original selection, never a shifted row or another set.
void LaunchDock::launch(const QString &id) {
	const int index = indexOf(id); if (index < 0 || stopped || demo || !valid || rows[index].busy) return;
	rows[index].busy = true; rows[index].state = "starting"; refresh();
	const auto entry = apps[index]; const auto selected = generation;
	auto *worker = QThread::create([this, entry, id, selected] {
		const auto result = startApp(entry);
		QMetaObject::invokeMethod(this, [this, id, selected, result] {
			const int index = indexOf(id); if (selected != generation || index < 0) return;
			rows[index].busy = false; rows[index].state = result; refresh();
		}, Qt::QueuedConnection);
	});
	workers.append(worker); worker->start();
}

// Changing a set cancels pending startup jobs and never launches or stops apps itself.
void LaunchDock::switchPreset(const QString &id) {
	if (!valid || id == library.activeId) return;
	int found = -1; for (int i = 0; i < library.presets.size(); ++i) if (library.presets[i].id == id) found = i;
	if (found < 0) return;
	persist(); if (!valid) return;
	++generation; library.activeId = id; apps = library.presets[found].apps;
	for (auto &entry : apps) if (entry.automatic && !demo) entry.path = detectInstalled(entry);
	for (auto &row : rows) { row.state = "ready"; row.busy = false; }
	persist(); rebuildRows(); status();
}

// A session starts once; scene/profile switches do not relaunch apps.
void LaunchDock::beginSession() {
	if (begun || stopped || !valid || demo) return;
	begun = true; const auto selected = generation;
	for (int i = 0; i < apps.size(); ++i) {
		if (!apps[i].enabled) continue;
		const auto id = apps[i].id;
		QTimer::singleShot(1200 + i * 800, this, [this, id, selected] { const int i = indexOf(id); if (selected == generation && i >= 0 && apps[i].enabled) launch(id); });
	}
}

// Release notifications cannot alter settings or run installers.
void LaunchDock::checkUpdates() {
	if (demo || stopped || update.state == "checking") return;
	update = {"checking", {}, {}}; refresh();
	updater->check(LAUNCH_DESK_VERSION, [this](UpdateResult result) { if (!stopped) { update = result; refresh(); } });
}

void LaunchDock::stop() { stopped = true; poll.stop(); updateTimer.stop(); updater->cancel(); }
LaunchDock::~LaunchDock() { stop(); for (auto *worker : workers) { worker->wait(); delete worker; } }
