// Single-file launcher for pdfreader.
//
// The real program and every DLL it needs are embedded in this executable as
// RCDATA resources. On first run they are unpacked into
// %LOCALAPPDATA%\PDFReader and the real program is started from there; later
// runs skip straight to launching, so the delay is paid once.
//
// Why a launcher instead of a self-extracting archive: an archive re-extracts
// ~60 MB into %TEMP% on every single launch. This extracts once and starts
// instantly afterwards.
//
// Built by tools/make_sfx.ps1. Resource layout:
//   1000  RCDATA  name list, one relative path per line, in resource-id order
//   1001  RCDATA  build stamp; extraction is skipped when it matches the marker
//   1..N  RCDATA  the payload files themselves
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int kNameListId  = 1000;
constexpr int kBuildStampId = 1001;

std::string ReadResource(int id, DWORD* sizeOut) {
    HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!found) return {};
    HGLOBAL loaded = LoadResource(nullptr, found);
    if (!loaded) return {};
    const DWORD size = SizeofResource(nullptr, found);
    const void* data = LockResource(loaded);
    if (!data || size == 0) return {};
    if (sizeOut) *sizeOut = size;
    return std::string(static_cast<const char*>(data), size);
}

// %LOCALAPPDATA% is set on every supported Windows. Reading the environment
// avoids pulling in shlobj's folder-id GUIDs, which MinGW exposes only through
// an extra library.
std::wstring LocalAppData() {
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (size == 0) return {};
    std::wstring out(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", out.data(), size);
    if (written == 0 || written >= size) return {};
    out.resize(written);
    return out;
}

bool WriteAll(const std::wstring& path, const char* data, DWORD size) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    const bool ok = size == 0 ||
                    (WriteFile(file, data, size, &written, nullptr) && written == size);
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

std::wstring ReadWholeFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    char buffer[256] = {};
    DWORD read = 0;
    ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
    CloseHandle(file);
    return std::wstring(buffer, buffer + read);
}

// Split on '\n' and normalise CRLF; leading/trailing blanks are dropped.
std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            while (!current.empty() && (current.back() == '\r' || current.back() == ' ')) {
                current.pop_back();
            }
            if (!current.empty()) lines.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    while (!current.empty() && (current.back() == '\r' || current.back() == ' ')) {
        current.pop_back();
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

bool EnsureDirectory(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return CreateDirectoryW(path.c_str(), nullptr) != 0 ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int need = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    if (need <= 0) return {};
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        out.data(), need);
    return out;
}

// Convert a relative payload path ("platforms\qwindows.dll") to a full path
// under `root`, creating intermediate directories.
bool MakePayloadPath(const std::wstring& root, const std::string& relative,
                     std::wstring* fullOut) {
    std::wstring full = root;
    std::wstring segment;
    for (size_t i = 0; i <= relative.size(); ++i) {
        const bool atEnd = (i == relative.size());
        const char c = atEnd ? '\\' : relative[i];
        if (c == '\\' || c == '/') {
            if (!segment.empty()) {
                full.push_back(L'\\');
                full += segment;
                // The last segment is the file itself; everything before it is
                // a directory that has to exist first.
                const bool isLast = atEnd;
                if (!isLast && !EnsureDirectory(full)) return false;
                segment.clear();
            }
        } else {
            segment.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
    }
    *fullOut = full;
    return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::string names = ReadResource(kNameListId, nullptr);
    const std::string stamp = ReadResource(kBuildStampId, nullptr);
    if (names.empty()) {
        MessageBoxW(nullptr, L"安装包已损坏：缺少文件清单。", L"PDF Reader", MB_ICONERROR);
        return 2;
    }

    const std::wstring localAppData = LocalAppData();
    if (localAppData.empty()) {
        MessageBoxW(nullptr, L"无法定位 %LOCALAPPDATA%。", L"PDF Reader", MB_ICONERROR);
        return 2;
    }

    const std::wstring dest = localAppData + L"\\PDFReader";
    const std::wstring markerPath = dest + L"\\.stamp";

    // Skip the whole unpack when this exact build is already in place. Kept as
    // named locals: comparing two temporaries here trips a GCC false positive
    // about freeing an unallocated object.
    bool needExtract = true;
    if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) {
        const std::wstring present = ReadWholeFile(markerPath);
        const std::wstring wanted  = Widen(stamp);
        needExtract = (present != wanted);
    }

    if (needExtract) {
        if (!EnsureDirectory(dest)) {
            MessageBoxW(nullptr, L"无法创建安装目录。", L"PDF Reader", MB_ICONERROR);
            return 2;
        }
        DeleteFileW(markerPath.c_str());   // a half-written marker must not count

        const std::vector<std::string> files = SplitLines(names);
        for (size_t i = 0; i < files.size(); ++i) {
            DWORD size = 0;
            const std::string blob = ReadResource(static_cast<int>(i) + 1, &size);
            if (blob.empty()) {
                MessageBoxW(nullptr, L"安装包已损坏：缺少组件。", L"PDF Reader",
                            MB_ICONERROR);
                return 2;
            }
            std::wstring target;
            if (!MakePayloadPath(dest, files[i], &target)) {
                MessageBoxW(nullptr, L"无法创建子目录。", L"PDF Reader", MB_ICONERROR);
                return 2;
            }
            // A file already in use (app running) is left alone rather than
            // failing the whole unpack.
            if (!WriteAll(target, blob.data(), size)) {
                if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    MessageBoxW(nullptr, L"无法写入程序文件。", L"PDF Reader",
                                MB_ICONERROR);
                    return 2;
                }
            }
        }
        WriteAll(markerPath, stamp.data(), static_cast<DWORD>(stamp.size()));
    }

    const std::wstring exe = dest + L"\\pdfreader.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"安装不完整，找不到主程序。", L"PDF Reader", MB_ICONERROR);
        return 2;
    }

    // Hand our own command line through so `PDFReader.exe file.pdf` works.
    std::wstring commandLine = L"\"" + exe + L"\"";
    if (PWSTR tail = GetCommandLineW()) {
        // Skip argv[0] of the launcher, honouring a quoted first token.
        PWSTR p = tail;
        if (*p == L'"') {
            ++p;
            while (*p && *p != L'"') ++p;
            if (*p == L'"') ++p;
        } else {
            while (*p && *p != L' ' && *p != L'\t') ++p;
        }
        commandLine += p;
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
    mutableCmd.push_back(L'\0');

    // bInheritHandles must be TRUE so the real program inherits our stdout and
    // stderr. Without it, `PDFReader.exe --selftest > log.txt` captures nothing.
    if (!CreateProcessW(exe.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE,
                        0, nullptr, dest.c_str(), &startup, &process)) {
        MessageBoxW(nullptr, L"无法启动主程序。", L"PDF Reader", MB_ICONERROR);
        return 3;
    }
    CloseHandle(process.hThread);

    // Wait for the real program and mirror its exit code. Without this,
    // `Start-Process -Wait` and scripts would return while the reader is still
    // running, and there would be no way to check its result.
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
