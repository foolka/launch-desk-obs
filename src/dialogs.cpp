#include "dock.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QDir>
#include <QStyle>
#include <QUuid>

// Use a themed child dialog with explicit confirmation instead of a native alert.
void LaunchDock::confirm(const QString &title, const QString &message, std::function<void()> action) {
	auto *dialog = new QDialog(this); dialog->setObjectName("confirm-remove"); dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setAttribute(Qt::WA_DontShowOnScreen, testAttribute(Qt::WA_DontShowOnScreen));
	dialog->setWindowTitle(title); dialog->setWindowModality(Qt::WindowModal); dialog->resize(420, 160);
	auto *layout = new QVBoxLayout(dialog);
	auto *label = new QLabel(message); label->setTextFormat(Qt::PlainText); label->setWordWrap(true); layout->addWidget(label);
	auto *buttons = new QDialogButtonBox;
	buttons->addButton(text("cancel"), QDialogButtonBox::RejectRole);
	auto *remove = buttons->addButton(text("delete"), QDialogButtonBox::AcceptRole); remove->setObjectName("confirm-delete");
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, action] { action(); dialog->accept(); });
	dialog->open();
}

// Keep edits in a draft until every field is validated and the user saves.
void LaunchDock::editApp(const QString &id) {
	if (!valid) return;
	const int index = indexOf(id);
	if ((!id.isEmpty() && (index < 0 || !apps[index].custom || rows[index].busy)) || (id.isEmpty() && apps.size() >= 104)) return;
	AppEntry draft;
	if (index >= 0) draft = apps[index];
	else { draft.id = "custom-" + QUuid::createUuid().toString(QUuid::WithoutBraces); draft.custom = true; draft.automatic = false; }
	const auto selected = generation;
	auto *dialog = new QDialog(this); dialog->setObjectName("app-editor"); dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setAttribute(Qt::WA_DontShowOnScreen, testAttribute(Qt::WA_DontShowOnScreen));
	dialog->setWindowTitle(text(id.isEmpty() ? "addApp" : "editApp")); dialog->setWindowModality(Qt::WindowModal); dialog->resize(520, 340);
	auto *layout = new QVBoxLayout(dialog);
	auto field = [this, layout](const QString &key, const QString &value, int limit) {
		layout->addWidget(new QLabel(text(key)));
		auto *input = new QLineEdit(value); input->setObjectName("app-" + key); input->setMaxLength(limit); input->setAccessibleName(text(key));
		return input;
	};
	auto *name = field("name", draft.name, 120); layout->addWidget(name);
	auto *path = field("program", draft.path, 32767); path->setReadOnly(true);
	auto *pathRow = new QHBoxLayout; pathRow->addWidget(path, 1);
	auto *browse = new QPushButton; browse->setObjectName("app-browse"); browse->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon)); browse->setToolTip(text("program")); browse->setAccessibleName(text("program")); pathRow->addWidget(browse); layout->addLayout(pathRow);
	auto *arguments = field("arguments", draft.arguments, 8192); layout->addWidget(arguments);
	auto *directory = field("directory", draft.workingDirectory, 32767);
	auto *dirRow = new QHBoxLayout; dirRow->addWidget(directory, 1);
	auto *dirBrowse = new QPushButton; dirBrowse->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon)); dirBrowse->setToolTip(text("directory")); dirBrowse->setAccessibleName(text("directory")); dirRow->addWidget(dirBrowse); layout->addLayout(dirRow);
	auto *error = new QLabel; error->setObjectName("app-error"); error->setWordWrap(true); error->hide(); layout->addWidget(error);
	auto *buttons = new QDialogButtonBox; buttons->addButton(text("cancel"), QDialogButtonBox::RejectRole);
	auto *save = buttons->addButton(text("save"), QDialogButtonBox::AcceptRole); save->setObjectName("app-save"); layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	connect(browse, &QPushButton::clicked, dialog, [this, dialog, name, path, arguments, directory, error, draft] {
		const auto file = QFileDialog::getOpenFileName(dialog, text("program"), QFileInfo(path->text()).absolutePath(), "Windows (*.exe *.lnk)");
		if (file.isEmpty()) return;
		auto entry = draft; entry.name = name->text();
		if (!selectProgram(file, entry)) { error->setText(text("invalidShortcut")); error->show(); return; }
		name->setText(entry.name); path->setText(entry.path); arguments->setText(entry.arguments); directory->setText(entry.workingDirectory); error->hide();
	});
	connect(dirBrowse, &QPushButton::clicked, dialog, [this, dialog, directory] {
		const auto folder = QFileDialog::getExistingDirectory(dialog, text("directory"), directory->text());
		if (!folder.isEmpty()) directory->setText(QDir::toNativeSeparators(folder));
	});
	connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, name, path, arguments, directory, error, draft, id, selected]() mutable {
		if (!valid || selected != generation) { dialog->reject(); return; }
		draft.name = name->text().trimmed(); draft.path = QDir::toNativeSeparators(path->text()); draft.exe = QFileInfo(draft.path).fileName();
		draft.arguments = arguments->text(); draft.workingDirectory = QDir::toNativeSeparators(directory->text().trimmed());
		if (!validCustomEntry(draft) || !validExecutable(draft.path)) { error->setText(text("invalidCustom")); error->show(); return; }
		if (!QFileInfo(launchDirectory(draft)).isDir()) { error->setText(text("directoryMissing")); error->show(); return; }
		const int i = indexOf(id);
		if (id.isEmpty()) apps.append(draft);
		else if (i >= 0 && !rows[i].busy) { apps[i] = draft; rows[i].state = "ready"; }
		else { dialog->reject(); return; }
		persist(); rebuildRows(); dialog->accept();
	});
	dialog->open(); name->setFocus();
}

// Removing an entry never uninstalls or terminates the selected program.
void LaunchDock::removeApp(const QString &id) {
	const int index = indexOf(id); if (!valid || index < 0 || !apps[index].custom || rows[index].busy) return;
	const auto selected = generation;
	confirm(text("removeApp"), text("deleteConfirm").arg(apps[index].name), [this, id, selected] {
		const int i = indexOf(id); if (!valid || selected != generation || i < 0 || rows[i].busy) return;
		apps.removeAt(i); persist(); rebuildRows();
	});
}

// Preset operations preserve the other sets and never trigger a launch.
void LaunchDock::editPreset(const QString &mode) {
	if (!valid) return;
	const auto id = library.activeId;
	int index = -1; for (int i = 0; i < library.presets.size(); ++i) if (library.presets[i].id == id) index = i;
	if (index < 0) return;
	if (mode == "deletePreset") {
		if (library.presets.size() <= 1) return;
		confirm(text(mode), text("deleteConfirm").arg(library.presets[index].name), [this, id] {
			if (!valid || library.activeId != id || library.presets.size() <= 1) return;
			for (int i = 0; i < library.presets.size(); ++i) if (library.presets[i].id == id) { library.presets.removeAt(i); break; }
			++generation; library.activeId = library.presets.first().id; apps = library.presets.first().apps;
			for (auto &entry : apps) if (entry.automatic && !demo) entry.path = detectInstalled(entry);
			for (auto &row : rows) { row.busy = false; row.state = "ready"; }
			persist(); rebuildRows(); status();
		});
		return;
	}
	if (mode != "renamePreset" && library.presets.size() >= 20) { notice->setText(text("limit")); notice->show(); return; }
	auto *dialog = new QDialog(this); dialog->setObjectName("preset-editor"); dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setAttribute(Qt::WA_DontShowOnScreen, testAttribute(Qt::WA_DontShowOnScreen));
	dialog->setWindowTitle(text(mode)); dialog->setWindowModality(Qt::WindowModal); dialog->resize(400, 160);
	auto *layout = new QVBoxLayout(dialog); layout->addWidget(new QLabel(text("name")));
	auto *name = new QLineEdit(mode == "newPreset" ? QString() : mode == "clonePreset" ? text("copyName").arg(library.presets[index].name) : library.presets[index].name);
	name->setObjectName("preset-name"); name->setMaxLength(80); layout->addWidget(name);
	auto *error = new QLabel; layout->addWidget(error);
	auto *buttons = new QDialogButtonBox; buttons->addButton(text("cancel"), QDialogButtonBox::RejectRole);
	auto *save = buttons->addButton(text("save"), QDialogButtonBox::AcceptRole); save->setObjectName("preset-save"); layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, name, error, mode, id] {
		if (!valid || library.activeId != id) { dialog->reject(); return; }
		const auto title = name->text().trimmed();
		if (title.isEmpty()) { error->setText(text("nameRequired")); return; }
		if (mode == "renamePreset") {
			for (auto &preset : library.presets) if (preset.id == id) preset.name = title;
			persist(); refresh();
		} else {
			persist(); if (!valid) return;
			LaunchPreset preset{QUuid::createUuid().toString(QUuid::WithoutBraces), title, mode == "clonePreset" ? apps : defaults()};
			library.presets.append(preset); switchPreset(preset.id);
		}
		dialog->accept();
	});
	dialog->open(); name->selectAll(); name->setFocus();
}
