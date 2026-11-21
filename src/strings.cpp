#include "dock.hpp"
#include <QMap>
#include <array>

// Language changes replace labels, never saved paths or app choices.
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
		{"language", {"Язык", "Мова", "Language"}},
		{"defaultPreset", {"Основной", "Основний", "Default"}},
		{"presets", {"Набор автозапуска", "Набір автозапуску", "Startup set"}},
		{"newPreset", {"Создать набор", "Створити набір", "Create set"}},
		{"clonePreset", {"Копировать набор", "Копіювати набір", "Duplicate set"}},
		{"renamePreset", {"Переименовать набор", "Перейменувати набір", "Rename set"}},
		{"deletePreset", {"Удалить набор", "Видалити набір", "Delete set"}},
		{"copyName", {"%1 · копия", "%1 · копія", "%1 · copy"}},
		{"addApp", {"Добавить приложение", "Додати програму", "Add application"}},
		{"editApp", {"Изменить приложение", "Змінити програму", "Edit application"}},
		{"removeApp", {"Удалить из набора", "Видалити з набору", "Remove from set"}},
		{"name", {"Название", "Назва", "Name"}},
		{"program", {"Приложение или ярлык", "Програма або ярлик", "Application or shortcut"}},
		{"arguments", {"Параметры запуска", "Параметри запуску", "Launch arguments"}},
		{"directory", {"Рабочая папка", "Робоча папка", "Working directory"}},
		{"directoryMissing", {"Рабочая папка не найдена", "Робочу папку не знайдено", "Working directory not found"}},
		{"invalidCustom", {"Проверьте название, локальный EXE и параметры запуска", "Перевірте назву, локальний EXE і параметри запуску", "Check the name, local EXE and launch arguments"}},
		{"invalidShortcut", {"Нужен EXE или ярлык на локальное EXE-приложение", "Потрібен EXE або ярлик на локальну EXE-програму", "Choose an EXE or a shortcut to a local EXE application"}},
		{"nameRequired", {"Введите название", "Введіть назву", "Enter a name"}},
		{"save", {"Сохранить", "Зберегти", "Save"}},
		{"cancel", {"Отмена", "Скасувати", "Cancel"}},
		{"delete", {"Удалить", "Видалити", "Delete"}},
		{"deleteConfirm", {"Удалить «%1»? Приложения и их данные останутся на компьютере.", "Видалити «%1»? Програми та їхні дані залишаться на комп'ютері.", "Remove “%1”? Applications and their data will stay on this computer."}},
		{"limit", {"Достигнут предел: 20 наборов, 100 своих приложений в наборе", "Досягнуто межі: 20 наборів, 100 власних програм у наборі", "Limit reached: 20 sets, 100 custom applications per set"}},
		{"checkUpdates", {"Проверить обновления", "Перевірити оновлення", "Check for updates"}},
		{"checking", {"Проверка обновлений…", "Перевірка оновлень…", "Checking for updates…"}},
		{"updateAvailable", {"Доступна новая версия %1", "Доступна нова версія %1", "Version %1 is available"}},
		{"upToDate", {"Установлена актуальная версия", "Встановлено актуальну версію", "You are up to date"}},
		{"updateFailed", {"Не удалось проверить обновления", "Не вдалося перевірити оновлення", "Could not check for updates"}},
		{"openRelease", {"Открыть обновление", "Відкрити оновлення", "Open update"}}
	};
	const auto found = copy.constFind(key);
	return found == copy.cend() ? key : (*found)[language == "ru" ? 0 : language == "uk" ? 1 : 2];
}
