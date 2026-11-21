#include "updates.hpp"
#include <windows.h>
#include <winhttp.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVersionNumber>
#include <QRegularExpression>
#include <QUrl>
#include <QElapsedTimer>
#include <memory>

// Accept only stable version tags and release links belonging to this product.
UpdateResult parseRelease(const QByteArray &bytes, const QString &currentVersion) {
	if (bytes.size() > 1048576) return {"updateFailed", {}, {}};
	QJsonParseError error;
	const auto object = QJsonDocument::fromJson(bytes, &error).object();
	const auto tag = object.value("tag_name").toString();
	const auto match = QRegularExpression("^v?(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$").match(tag);
	const QUrl url(object.value("html_url").toString());
	if (error.error != QJsonParseError::NoError || !match.hasMatch() || object.value("draft").toBool() || object.value("prerelease").toBool() ||
		url.scheme() != "https" || url.host() != "github.com" || !url.userInfo().isEmpty() || url.port() != -1 ||
		url.path() != "/foolka/launch-desk-obs/releases/tag/" + tag || url.hasQuery() || url.hasFragment()) return {"updateFailed", {}, {}};
	const auto version = tag.startsWith('v') ? tag.mid(1) : tag;
	return QVersionNumber::fromString(version) > QVersionNumber::fromString(currentVersion) ? UpdateResult{"updateAvailable", version, url.toString()} : UpdateResult{"upToDate", {}, {}};
}

// Use Windows HTTPS, independent of the optional TLS plugins bundled with OBS.
static UpdateResult fetchRelease(const QString &version, const std::atomic_bool &cancelled) {
	const UpdateResult failed{"updateFailed", {}, {}};
	using InternetHandle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;
	const auto agent = QString("Launch-Desk-OBS/") + version;
	InternetHandle session(WinHttpOpen(reinterpret_cast<LPCWSTR>(agent.utf16()), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), WinHttpCloseHandle);
	if (!session || !WinHttpSetTimeouts(session.get(), 2000, 2000, 2000, 2000) || cancelled) return failed;
	InternetHandle connection(WinHttpConnect(session.get(), L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0), WinHttpCloseHandle);
	if (!connection || cancelled) return failed;
	InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", L"/repos/foolka/launch-desk-obs/releases/latest", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), WinHttpCloseHandle);
	if (!request) return failed;
	DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER, retries = 1;
	if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) ||
		!WinHttpSetOption(request.get(), WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries))) return failed;
	QElapsedTimer elapsed; elapsed.start();
	if (cancelled || !WinHttpSendRequest(request.get(), L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		cancelled || !WinHttpReceiveResponse(request.get(), nullptr)) return failed;
	DWORD status = 0, size = sizeof(status);
	if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200) return failed;
	QByteArray bytes;
	while (!cancelled && elapsed.elapsed() < 15000) {
		char buffer[16384]; DWORD received = 0;
		if (!WinHttpReadData(request.get(), buffer, sizeof(buffer), &received)) return failed;
		if (!received) return parseRelease(bytes, version);
		bytes.append(buffer, static_cast<int>(received));
		if (bytes.size() > 1048576) return failed;
	}
	return failed;
}

// The bounded request runs outside OBS's UI thread and never executes installers.
void UpdateChecker::check(const QString &version, std::function<void(UpdateResult)> callback) {
	if (worker) return;
	cancelled = false;
	const auto selected = ++generation;
	worker = QThread::create([this, version, callback, selected] {
		const auto result = fetchRelease(version, cancelled);
		QMetaObject::invokeMethod(this, [this, result, callback, selected] {
			if (cancelled || selected != generation) return;
			worker->wait(); delete worker; worker = nullptr;
			callback(result);
		}, Qt::QueuedConnection);
	});
	worker->start();
}

// Join before plugin unload so no network worker can execute code from an unloaded DLL.
void UpdateChecker::cancel() {
	++generation;
	cancelled = true;
	if (worker) { worker->wait(); delete worker; worker = nullptr; }
}
