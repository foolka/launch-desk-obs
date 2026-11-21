#include <windows.h>
#include <shellapi.h>
#include <string>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	int count = 0;
	auto **args = CommandLineToArgvW(GetCommandLineW(), &count);
	if (args && count >= 3 && std::wstring(args[1]) == L"--report") {
		wchar_t directory[32768]{}; GetCurrentDirectoryW(32768, directory);
		std::wstring report(directory); report += L"\n";
		for (int i = 3; i < count; ++i) { report += args[i]; report += L"\n"; }
		HANDLE file = CreateFileW(args[2], GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE) {
			DWORD written = 0; WriteFile(file, report.data(), static_cast<DWORD>(report.size() * sizeof(wchar_t)), &written, nullptr); CloseHandle(file);
		}
	}
	if (args) LocalFree(args);
	Sleep(6000);
	return 0;
}
