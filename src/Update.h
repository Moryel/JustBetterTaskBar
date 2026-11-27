// Updates from GitHub releases without the dock ever going away: the new executable is
// downloaded next to the running one, checked against the SHA-256 digest GitHub publishes for
// it, swapped in, and started with --takeover. The new instance shows itself on top of the old
// one and then asks it to quit without bringing the Windows taskbar back.
#pragma once
#include <winhttp.h>
#include <bcrypt.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include "version.h"
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace update {

enum State { Idle, Checking, UpToDate, Available, Working, Failed };
inline std::atomic<int> state{Idle};
inline std::mutex mtx;
inline std::wstring latest, message, pageUrl, exeUrl, exeDigest;  // guarded by mtx

static const wchar_t* kApi = L"https://api.github.com/repos/Moryel/JustBetterTaskBar/releases/latest";
static const wchar_t* kPage = L"https://github.com/Moryel/JustBetterTaskBar/releases/latest";
// the installer's uninstall entry, so Settings > Apps shows the new version after an update
static const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{CF23F21B-2949-43D6-9CEE-0C80440E3747}_is1";

static std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static bool HttpGet(const std::wstring& url, std::string& out, size_t maxBytes, bool api) {
    wchar_t host[256] = {}, path[2048] = {}, extra[2048] = {};
    URL_COMPONENTS uc{sizeof uc};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
    std::wstring target = std::wstring(path) + extra;
    HINTERNET s = WinHttpOpen(L"JustBetterTaskBar-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    bool ok = false;
    if (HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0)) {
        if (HINTERNET r = WinHttpOpenRequest(c, L"GET", target.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE)) {
            const wchar_t* hdr = api ? L"Accept: application/vnd.github+json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS;
            if (WinHttpSendRequest(r, hdr, api ? (DWORD)-1L : 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
                DWORD status = 0, sz = sizeof status;
                WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, nullptr);
                if (status == 200) {
                    ok = true;
                    for (;;) {
                        DWORD avail = 0, got = 0;
                        if (!WinHttpQueryDataAvailable(r, &avail)) { ok = false; break; }
                        if (!avail) break;
                        size_t at = out.size();
                        if (at + avail > maxBytes) { ok = false; break; }
                        out.resize(at + avail);
                        if (!WinHttpReadData(r, &out[at], avail, &got)) { ok = false; break; }
                        out.resize(at + got);
                    }
                }
            }
            WinHttpCloseHandle(r);
        }
        WinHttpCloseHandle(c);
    }
    WinHttpCloseHandle(s);
    return ok;
}

// The string value of the first "key" at or after `from`; empty when missing or not a string.
// Enough for the plain fields read here (tags, URLs, digests never contain escapes).
static std::string JsonStr(const std::string& j, const char* key, size_t from = 0) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos) return {};
    p = j.find(':', p + k.size());
    if (p == std::string::npos) return {};
    p = j.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos || j[p] != '"') return {};
    size_t e = j.find('"', p + 1);
    return e == std::string::npos ? std::string() : j.substr(p + 1, e - p - 1);
}

static bool IsNewer(const std::wstring& tag) {
    int v[3] = {0, 0, 0}, i = 0;
    for (wchar_t ch : tag) {
        if (ch >= L'0' && ch <= L'9') v[i] = v[i] * 10 + (ch - L'0');
        else if (ch == L'.' && i < 2) i++;
    }
    int cur[3] = {JBT_VERSION_MAJOR, JBT_VERSION_MINOR, JBT_VERSION_PATCH};
    for (int k = 0; k < 3; k++)
        if (v[k] != cur[k]) return v[k] > cur[k];
    return false;
}

static bool DigestMatches(const std::string& data, const std::wstring& digest) {
    if (digest.size() != 7 + 64 || digest.compare(0, 7, L"sha256:") != 0) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    UCHAR out[32] = {};
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0 &&
              BCryptHashData(h, (PUCHAR)data.data(), (ULONG)data.size(), 0) == 0 &&
              BCryptFinishHash(h, out, 32, 0) == 0;
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return false;
    wchar_t hex[65];
    for (int i = 0; i < 32; i++) swprintf(hex + i * 2, 3, L"%02x", out[i]);
    return _wcsicmp(hex, digest.c_str() + 7) == 0;
}

// Puts the new executable in place of the running one (Windows allows renaming a running
// exe). The old file is left as .old and removed by the new instance once this one has quit.
static std::wstring Swap(const std::wstring& exe, const std::string& data, const std::wstring& ver) {
    std::wstring fresh = exe + L".new", old = exe + L".old";
    HANDLE f = CreateFileW(fresh.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"the app's folder can't be written to";
    DWORD written = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
    CloseHandle(f);
    if (!ok) { DeleteFileW(fresh.c_str()); return L"the update couldn't be saved"; }
    DeleteFileW(old.c_str());
    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(fresh.c_str());
        return L"the running version couldn't be replaced";
    }
    if (!MoveFileExW(fresh.c_str(), exe.c_str(), 0)) {
        MoveFileExW(old.c_str(), exe.c_str(), 0);
        DeleteFileW(fresh.c_str());
        return L"the running version couldn't be replaced";
    }
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        std::wstring v = !ver.empty() && (ver[0] == L'v' || ver[0] == L'V') ? ver.substr(1) : ver;
        RegSetValueExW(k, L"DisplayVersion", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
    return {};
}

// Looks for a newer release in the background. Posts `msg` to `notify` when done;
// wParam is 1 for the quiet startup check, 0 when the user asked.
inline void Check(HWND notify, UINT msg, bool quiet) {
    int s = state.load();
    if (s == Checking || s == Working) return;
    state = Checking;
    std::thread([notify, msg, quiet] {
        std::string j;
        std::wstring tag, err, page, url, digest;
        int result = Failed;
        if (HttpGet(kApi, j, 4u << 20, true)) {
            tag = Wide(JsonStr(j, "tag_name"));
            page = Wide(JsonStr(j, "html_url"));
            size_t a = j.find("\"name\":\"JustBetterTaskBar.exe\"");
            if (a != std::string::npos) {
                url = Wide(JsonStr(j, "browser_download_url", a));
                digest = Wide(JsonStr(j, "digest", a));
            }
            if (tag.empty()) err = L"no release was found";
            else result = IsNewer(tag) ? Available : UpToDate;
        } else {
            err = L"GitHub couldn't be reached";
        }
        {
            std::lock_guard<std::mutex> lk(mtx);
            latest = tag;
            message = err;
            pageUrl = page.empty() ? kPage : page;
            exeUrl = url;
            exeDigest = digest;
        }
        state = result;
        PostMessageW(notify, msg, quiet ? 1 : 0, 0);
    }).detach();
}

// Downloads, verifies and swaps in the new version, then starts it with --takeover.
// Releases without a bare executable just open the download page instead.
inline void Install(HWND notify, UINT msg, const std::wstring& exePath) {
    if (state == Working) return;
    std::wstring url, digest, ver, page;
    {
        std::lock_guard<std::mutex> lk(mtx);
        url = exeUrl;
        digest = exeDigest;
        ver = latest;
        page = pageUrl.empty() ? kPage : pageUrl;
    }
    if (url.empty() || digest.empty()) {
        ShellExecuteW(nullptr, L"open", page.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    state = Working;
    std::thread([notify, msg, exePath, url, digest, ver] {
        std::string data;
        std::wstring err;
        if (!HttpGet(url, data, 64u << 20, false)) err = L"the download failed";
        else if (!DigestMatches(data, digest)) err = L"the download didn't match its checksum";
        else err = Swap(exePath, data, ver);
        if (err.empty()) {
            STARTUPINFOW si{sizeof si};
            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"\"" + exePath + L"\" --takeover";
            if (CreateProcessW(exePath.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            } else {
                err = L"the new version couldn't be started";
            }
        }
        if (!err.empty()) {
            std::lock_guard<std::mutex> lk(mtx);
            message = err;
            state = Failed;
        }
        PostMessageW(notify, msg, 2, 0);
    }).detach();
}

}  // namespace update
