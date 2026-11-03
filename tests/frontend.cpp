#include <QWidget>
static QWidget *dock = nullptr;
static void (*callback)(int, void *) = nullptr;
extern "C" __declspec(dllexport) bool obs_frontend_add_dock_by_id(const char *, const char *, void *widget) {
	dock = static_cast<QWidget *>(widget); return true;
}
extern "C" __declspec(dllexport) void obs_frontend_remove_dock(const char *) { delete dock; dock = nullptr; }
extern "C" __declspec(dllexport) void obs_frontend_add_event_callback(void(*cb)(int, void *), void *) { callback = cb; }
extern "C" __declspec(dllexport) void obs_frontend_remove_event_callback(void(*)(int, void *), void *) { callback = nullptr; }
extern "C" __declspec(dllexport) void test_event(int value) { if (callback) callback(value, nullptr); }
extern "C" __declspec(dllexport) QWidget *test_dock() { return dock; }
