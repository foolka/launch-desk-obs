#pragma once
#include <QObject>
#include <QThread>
#include <atomic>
#include <functional>

struct UpdateResult { QString state, version, url; };
UpdateResult parseRelease(const QByteArray &bytes, const QString &currentVersion);

class UpdateChecker : public QObject {
public:
	explicit UpdateChecker(QObject *parent = nullptr) : QObject(parent) {}
	~UpdateChecker() override { cancel(); }
	void cancel();
	void check(const QString &version, std::function<void(UpdateResult)> callback);
private:
	QThread *worker = nullptr;
	unsigned generation = 0;
	std::atomic_bool cancelled{false};
};
