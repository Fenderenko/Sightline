#include "settings/update.h"

#include "common/ipc.h"
#include "common/json.h"
#include "common/winutil.h"

#include <winhttp.h>

#include <cstdio>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "version.lib")

namespace sl::update {

namespace {

constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kApiPath[] = L"/repos/Fenderenko/Sightline/releases/latest";
// The zip is accepted only from this repository's release downloads.
constexpr wchar_t kDownloadPrefix[] = L"https://github.com/Fenderenko/Sightline/releases/download/";
constexpr size_t kMaxApiBody = 1 << 20;
constexpr size_t kMaxZip = 64 << 20;

struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
};

// HTTPS GET with redirects (GitHub serves release assets from another host). Proxy settings come
// from Windows. The user agent names the app only.
bool HttpsGet(const std::wstring& url, std::string& body, size_t maxBytes) {
  URL_COMPONENTS uc{sizeof(uc)};
  wchar_t host[256] = {}, path[2048] = {};
  uc.lpszHostName = host;
  uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return false;

  Handle session{WinHttpOpen(L"Sightline-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.h) return false;
  WinHttpSetTimeouts(session.h, 10000, 10000, 10000, 20000);
  Handle connect{WinHttpConnect(session.h, host, uc.nPort, 0)};
  if (!connect.h) return false;
  Handle request{WinHttpOpenRequest(connect.h, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                    WINHTTP_FLAG_SECURE)};
  if (!request.h) return false;
  const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
  if (!WinHttpSendRequest(request.h, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    return false;
  }
  DWORD status = 0, size = sizeof(status);
  if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                           &status, &size, WINHTTP_NO_HEADER_INDEX) ||
      status != 200) {
    return false;
  }
  body.clear();
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(request.h, &avail)) return false;
    if (avail == 0) break;
    if (body.size() + avail > maxBytes) return false;
    const size_t at = body.size();
    body.resize(at + avail);
    DWORD read = 0;
    if (!WinHttpReadData(request.h, body.data() + at, avail, &read)) return false;
    body.resize(at + read);
  }
  return true;
}

bool FileExists(const std::wstring& path) {
  const DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool RunHidden(std::wstring cmd, DWORD timeoutMs) {
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    return false;
  }
  CloseHandle(pi.hThread);
  const bool done = WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0;
  DWORD code = 1;
  if (done) GetExitCodeProcess(pi.hProcess, &code);
  else TerminateProcess(pi.hProcess, 1);
  CloseHandle(pi.hProcess);
  return done && code == 0;
}

void Launch(const std::wstring& exe, const std::wstring& args) {
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
  if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

// Closes the running core and waits for its process to exit, so its exe can be replaced.
void StopCore() {
  const HWND w = ipc::CoreWindow();
  if (!w) return;
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  const HANDLE process = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
  PostMessageW(w, WM_CLOSE, 0, 0);
  if (process) {
    WaitForSingleObject(process, 5000);
    CloseHandle(process);
  }
}

}  // namespace

std::string CurrentVersion() {
  wchar_t path[MAX_PATH];
  if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return "0";
  DWORD ignored = 0;
  const DWORD size = GetFileVersionInfoSizeW(path, &ignored);
  std::vector<BYTE> data(size);
  VS_FIXEDFILEINFO* info = nullptr;
  UINT len = 0;
  if (!size || !GetFileVersionInfoW(path, 0, size, data.data()) ||
      !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info) {
    return "0";
  }
  char buf[48];
  snprintf(buf, sizeof(buf), "%u.%u.%u", HIWORD(info->dwProductVersionMS), LOWORD(info->dwProductVersionMS),
           HIWORD(info->dwProductVersionLS));
  return buf;
}

int CompareVersions(std::string_view a, std::string_view b) {
  const auto next = [](std::string_view& s) {
    long v = 0;
    while (!s.empty() && s.front() >= '0' && s.front() <= '9') {
      v = v * 10 + (s.front() - '0');
      s.remove_prefix(1);
    }
    if (!s.empty()) s.remove_prefix(1);  // separator
    return v;
  };
  while (!a.empty() || !b.empty()) {
    const long x = next(a), y = next(b);
    if (x != y) return x < y ? -1 : 1;
  }
  return 0;
}

bool FetchLatest(Release& out) {
  std::string body;
  if (!HttpsGet(std::wstring(L"https://") + kApiHost + kApiPath, body, kMaxApiBody)) return false;
  json::Value v;
  if (!json::Parse(body, v) || !v.IsObject()) return false;
  std::string tag = v["tag_name"].Str("");
  if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
  if (tag.empty() || tag[0] < '0' || tag[0] > '9') return false;
  out.version = tag;
  out.zipUrl.clear();
  const std::string name = "Sightline-v" + tag + "-win-x64.zip";
  const json::Value& assets = v["assets"];
  for (size_t i = 0; i < assets.Size(); ++i) {
    const json::Value& a = assets.At(i);
    if (a["name"].Str("") != name) continue;
    const std::wstring url = Widen(a["browser_download_url"].Str(""));
    if (url.rfind(kDownloadPrefix, 0) == 0) out.zipUrl = url;
  }
  return true;
}

bool Download(const Release& r, std::wstring& dir) {
  if (r.zipUrl.empty()) return false;
  std::string zip;
  if (!HttpsGet(r.zipUrl, zip, kMaxZip) || zip.size() < 4 || zip[0] != 'P' || zip[1] != 'K') return false;

  wchar_t temp[MAX_PATH];
  if (!GetTempPathW(MAX_PATH, temp)) return false;
  dir = std::wstring(temp) + L"Sightline-update-" + Widen(r.version) + L"-" + std::to_wstring(GetTickCount64());
  if (!CreateDirectoryW(dir.c_str(), nullptr)) return false;
  const std::wstring zipPath = dir + L".zip";
  HANDLE f = CreateFileW(zipPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(f, zip.data(), static_cast<DWORD>(zip.size()), &written, nullptr) && written == zip.size();
  CloseHandle(f);
  if (!ok) return false;

  // tar.exe (bsdtar) ships with Windows 10 1803+ and unpacks zip archives.
  wchar_t sys[MAX_PATH];
  if (!GetSystemDirectoryW(sys, MAX_PATH)) return false;
  const std::wstring tar = std::wstring(sys) + L"\\tar.exe";
  const bool unpacked = FileExists(tar) && RunHidden(L"\"" + tar + L"\" -xf \"" + zipPath + L"\" -C \"" + dir + L"\"", 60000);
  DeleteFileW(zipPath.c_str());
  if (!unpacked) return false;
  return FileExists(dir + L"\\" + ipc::kCoreExe) && FileExists(dir + L"\\" + ipc::kSettingsExe);
}

bool Install(const std::wstring& dir) {
  const std::wstring home = ExeDir();
  const wchar_t* exes[] = {ipc::kCoreExe, ipc::kSettingsExe};
  StopCore();
  // A running exe can be renamed but not overwritten: move each aside, then move the new one in.
  int moved = 0;
  bool ok = true;
  for (const wchar_t* exe : exes) {
    const std::wstring target = home + L"\\" + exe, old = target + L".old";
    DeleteFileW(old.c_str());
    if (!MoveFileExW(target.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      ok = false;
      break;
    }
    if (!MoveFileExW((dir + L"\\" + exe).c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
      MoveFileExW(old.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
      ok = false;
      break;
    }
    ++moved;
  }
  if (!ok) {
    for (int i = 0; i < moved; ++i) {
      const std::wstring target = home + L"\\" + exes[i];
      MoveFileExW((target + L".old").c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    Log(L"settings", L"update: install failed (%lu)", GetLastError());
    Launch(home + L"\\" + ipc::kCoreExe, {});
    return false;
  }
  Log(L"settings", L"update: installed from %s", dir.c_str());
  DeleteFileW((dir + L"\\README.md").c_str());
  RemoveDirectoryW(dir.c_str());
  Launch(home + L"\\" + ipc::kCoreExe, {});
  Launch(home + L"\\" + ipc::kSettingsExe, L"--after-update");
  return true;
}

}  // namespace sl::update
