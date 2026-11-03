#include "dock.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QStyle>
#include <QFrame>
#include <QFileInfo>
#include <QPointer>
#include <QDir>
#include <QMap>
#include <array>

// Keep all UI text together; language changes do not touch app settings.
QString LaunchDock::text(const QString &key) const {
	static const QMap<QString, std::array<QString, 3>> copy = {
		{"auto", {"Запускать вместе с OBS", "Запускати разом з OBS", "Start with OBS"}},
		{"tray", {"Запускать в трей", "Запускати у трей", "Start in system tray"}},
		{"detected", {"Автопоиск", "Автопошук", "Auto-detected"}},
		{"manual", {"Выбран вручную", "Вибрано вручну", "Manual selection"}},
		{"browse", {"Выбрать EXE", "Вибрати EXE", "Choose EXE"}},
		{"detect", {"Найти установленное приложение", "Знайти встановлену програму", "Find installed app"}},
		{"launch", {"Запустить сейчас", "Запустити зараз", "Start now"}},
		{"missing", {"Не найдено: выберите файл приложения", "Не знайдено: виберіть файл програми", "Not found: choose the app executable"}},
		{"ready", {"Готово к запуску", "Готово до запуску", "Ready to start"}},
		{"running", {"Уже работает", "Уже працює", "Already running"}},
		{"different", {"Работает другая копия: повторный запуск отменён", "Працює інша копія: повторний запуск скасовано", "Another copy is running: launch skipped"}},
		{"unknown", {"Не удалось проверить процесс: запуск отменён", "Не вдалося перевірити процес: запуск скасовано", "Cannot check process: launch skipped"}},
		{"starting", {"Запускается…", "Запускається…", "Starting…"}},
		{"unconfirmed", {"Запуск отправлен, процесс не подтверждён", "Запуск надіслано, процес не підтверджено", "Launch sent, process not confirmed"}},
		{"busy", {"Запуск уже выполняется", "Запуск уже виконується", "A launch is already in progress"}},
		{"failed", {"Не удалось запустить. Проверьте файл и Проводник Windows", "Не вдалося запустити. Перевірте файл і Провідник Windows", "Could not start. Check the file and Windows Explorer"}},
		{"identity", {"Запустите OBS под своим пользователем Windows", "Запустіть OBS під своїм користувачем Windows", "Run OBS as your desktop Windows user"}},
		{"settingsError", {"Настройки недоступны или повреждены. Файл сохранён без изменений; автозапуск отключён", "Налаштування недоступні або пошкоджені. Файл не змінено; автозапуск вимкнено", "Settings unavailable or damaged. File preserved; autostart disabled"}},
		{"saveError", {"Не удалось сохранить настройки; автозапуск отключён", "Не вдалося зберегти налаштування; автозапуск вимкнено", "Cannot save settings; autostart disabled"}},
		{"invalid", {"Выберите %1, а не установщик или OBS-плагин", "Виберіть %1, а не інсталятор чи OBS-плагін", "Choose %1, not an installer or OBS plugin"}},
		{"language", {"Язык", "Мова", "Language"}}
	};
	const auto found = copy.constFind(key);
	return found == copy.cend() ? key : (*found)[language == "ru" ? 0 : language == "uk" ? 1 : 2];
}

// The dock uses OBS's palette; no web server, browser source or Twitch login is needed.
LaunchDock::LaunchDock(const QString &profile, const QString &locale, bool preview) :
	file(profile + "/settings.json"), language(locale.left(2)), demo(preview) {
	if (!QStringList{"ru", "uk", "en"}.contains(language)) language = "en";
	valid = demo || loadConfig(file, apps, language);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->setSpacing(12);
	setMinimumWidth(280);
	auto *heading = new QHBoxLayout;
	auto *title = new QLabel("Launch Desk <small>0.1.1</small>");
	heading->addWidget(title, 1);
	languages = new QComboBox;
	languages->setObjectName("language");
	languages->addItem("Русский", "ru"); languages->addItem("Українська", "uk"); languages->addItem("English", "en");
	languages->setCurrentIndex(languages->findData(language));
	heading->addWidget(languages);
	layout->addLayout(heading);
	for (int index = 0; index < apps.size(); ++index) {
		auto &entry = apps[index];
		if (entry.automatic) entry.path = demo ? "C:\\Users\\Streamer\\AppData\\Local\\Programs\\" + entry.name + "\\" + entry.exe : detectInstalled(entry);
		if (demo) entry.enabled = true;
		auto *section = new QVBoxLayout;
		section->setSpacing(7);
		auto *name = new QLabel(entry.name);
		auto font = name->font(); font.setBold(true); name->setFont(font);
		section->addWidget(name);
		Row row{};
		row.enabled = new QCheckBox;
		row.enabled->setObjectName(entry.id + "-enabled");
		row.enabled->setChecked(entry.enabled);
		row.enabled->setEnabled(valid);
		section->addWidget(row.enabled);
		row.inTray = new QCheckBox;
		row.inTray->setObjectName(entry.id + "-tray");
		row.inTray->setChecked(entry.inTray);
		row.inTray->setEnabled(valid);
		section->addWidget(row.inTray);
		row.path = new QLineEdit(entry.path);
		row.path->setObjectName(entry.id + "-path");
		row.path->setReadOnly(true);
		row.path->setCursorPosition(0);
		auto *pathRow = new QHBoxLayout;
		pathRow->setSpacing(5);
		pathRow->addWidget(row.path, 1);
		row.browse = new QPushButton;
		row.browse->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
		row.detect = new QPushButton;
		row.detect->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
		for (auto *button : {row.browse, row.detect}) { button->setFixedSize(30, 30); button->setEnabled(valid); pathRow->addWidget(button); }
		section->addLayout(pathRow);
		auto *bottom = new QHBoxLayout;
		row.mode = new QLabel;
		bottom->addWidget(row.mode, 1);
		row.launch = new QPushButton;
		row.launch->setObjectName(entry.id + "-launch");
		row.launch->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
		row.launch->setEnabled(valid && !demo);
		bottom->addWidget(row.launch);
		section->addLayout(bottom);
		row.status = new QLabel;
		row.status->setObjectName(entry.id + "-status");
		row.status->setWordWrap(true);
		row.state = demo ? (index == 0 ? "running" : "ready") : entry.path.isEmpty() ? "missing" : "ready";
		section->addWidget(row.status);
		rows.append(row);
		layout->addLayout(section);
		if (index == 0) { auto *line = new QFrame; line->setFrameShape(QFrame::HLine); layout->addWidget(line); }
		connect(row.enabled, &QCheckBox::toggled, this, [this, index](bool value) { apps[index].enabled = value; persist(); });
		connect(row.inTray, &QCheckBox::toggled, this, [this, index](bool value) { apps[index].inTray = value; persist(); });
		connect(row.browse, &QPushButton::clicked, this, [this, index] {
			const auto selected = QFileDialog::getOpenFileName(this, text("browse"), QFileInfo(apps[index].path).absolutePath(), apps[index].exe + " (" + apps[index].exe + ")");
			if (selected.isEmpty()) return;
			if (!validExecutable(selected, apps[index].exe)) { notice->setText(text("invalid").arg(apps[index].exe)); notice->show(); return; }
			apps[index].path = QDir::toNativeSeparators(selected); apps[index].automatic = false;
			rows[index].state = "ready"; persist(); refresh();
		});
		connect(row.detect, &QPushButton::clicked, this, [this, index] {
			apps[index].path = detectInstalled(apps[index]); apps[index].automatic = true;
			rows[index].state = apps[index].path.isEmpty() ? "missing" : "ready"; persist(); refresh();
		});
		connect(row.launch, &QPushButton::clicked, this, [this, index] { launch(index); });
	}
	notice = new QLabel;
	notice->setWordWrap(true);
	notice->setVisible(!valid);
	layout->addWidget(notice);
	layout->addStretch();
	connect(languages, &QComboBox::currentIndexChanged, this, [this] { language = languages->currentData().toString(); persist(); refresh(); });
	connect(&poll, &QTimer::timeout, this, [this] { status(); });
	if (!demo) poll.start(5000);
	refresh();
}

// Settings are changed only by an explicit user action, never just by opening OBS.
void LaunchDock::persist() {
	if (demo || !valid) return;
	if (!saveConfig(file, apps, language)) {
		valid = false;
		notice->setText(text("saveError")); notice->show();
		for (auto &row : rows) { row.enabled->setEnabled(false); row.inTray->setEnabled(false); row.browse->setEnabled(false); row.detect->setEnabled(false); row.launch->setEnabled(false); }
	}
}

// Update labels without rebuilding controls or stealing keyboard focus.
void LaunchDock::refresh() {
	languages->setAccessibleName(text("language"));
	for (int i = 0; i < rows.size(); ++i) {
		auto &row = rows[i];
		row.enabled->setText(text("auto"));
		row.inTray->setText(text("tray"));
		row.path->setText(apps[i].path); row.path->setCursorPosition(0); row.path->setToolTip(apps[i].path);
		row.path->setAccessibleName(apps[i].name + " EXE");
		row.mode->setText(text(apps[i].automatic ? "detected" : "manual"));
		row.browse->setToolTip(text("browse")); row.browse->setAccessibleName(text("browse"));
		row.detect->setToolTip(text("detect")); row.detect->setAccessibleName(text("detect"));
		row.launch->setText(text("launch")); row.status->setText(text(row.state));
	}
	if (!valid) notice->setText(text("settingsError"));
}

// A stopped or failed application is reported, not automatically restarted in a loop.
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

// COM and process confirmation run outside OBS's UI thread.
void LaunchDock::launch(int index) {
	if (stopped || demo || !valid || rows[index].busy) return;
	rows[index].busy = true;
	rows[index].state = "starting";
	rows[index].launch->setEnabled(false);
	refresh();
	const auto entry = apps[index];
	auto *worker = QThread::create([this, entry, index] {
		const auto result = startApp(entry);
		QMetaObject::invokeMethod(this, [this, index, result] {
			rows[index].busy = false; rows[index].state = result;
			rows[index].launch->setEnabled(valid && !stopped); refresh();
		}, Qt::QueuedConnection);
	});
	workers.append(worker);
	worker->start();
}

// OBS sends FINISHED_LOADING once; later scene/profile switches must not relaunch apps.
void LaunchDock::beginSession() {
	if (begun || stopped || !valid || demo) return;
	begun = true;
	for (int i = 0; i < apps.size(); ++i) {
		if (!apps[i].enabled) continue;
		QTimer::singleShot(1200 + i * 800, this, [this, i] { if (!stopped && apps[i].enabled) launch(i); });
	}
}

// Closing OBS never closes the user's applications.
void LaunchDock::stop() { stopped = true; poll.stop(); }

LaunchDock::~LaunchDock() {
	stop();
	for (auto *worker : workers) { worker->wait(); delete worker; }
}
