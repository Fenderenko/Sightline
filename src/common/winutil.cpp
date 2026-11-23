#include "common/winutil.h"

#include <shlobj.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace sl {

std::wstring Widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string Narrow(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Lower(std::wstring_view s) {
  std::wstring r(s);
  if (!r.empty()) CharLowerBuffW(r.data(), static_cast<DWORD>(r.size()));
  return r;
}

std::wstring Upper(std::wstring_view s) {
  std::wstring r(s);
  if (!r.empty()) CharUpperBuffW(r.data(), static_cast<DWORD>(r.size()));
  return r;
}

std::wstring FileNameOf(std::wstring_view path) {
  const size_t pos = path.find_last_of(L"\\/");
  return std::wstring(pos == std::wstring_view::npos ? path : path.substr(pos + 1));
}

std::wstring ExeDir() {
  wchar_t buf[MAX_PATH * 2];
  const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
  std::wstring p(buf, n);
  const size_t pos = p.find_last_of(L"\\/");
  return pos == std::wstring::npos ? p : p.substr(0, pos);
}

static std::wstring KnownFolderSub(REFKNOWNFOLDERID id, const wchar_t* sub) {
  PWSTR raw = nullptr;
  std::wstring r;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw))) {
    r = raw;
    CoTaskMemFree(raw);
  }
  r += L"\\";
  r += sub;
  SHCreateDirectoryExW(nullptr, r.c_str(), nullptr);
  return r;
}

std::wstring ConfigDir() {
  static const std::wstring dir = KnownFolderSub(FOLDERID_RoamingAppData, L"Sightline");
  return dir;
}

std::wstring LogDir() {
  static const std::wstring dir = KnownFolderSub(FOLDERID_LocalAppData, L"Sightline\\logs");
  return dir;
}

std::wstring ProcessExeName(DWORD pid) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return {};
  wchar_t buf[1024];
  DWORD n = static_cast<DWORD>(std::size(buf));
  std::wstring r;
  if (QueryFullProcessImageNameW(h, 0, buf, &n)) r = Lower(FileNameOf(std::wstring_view(buf, n)));
  CloseHandle(h);
  return r;
}

std::wstring WindowExeName(HWND hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  return pid ? ProcessExeName(pid) : std::wstring();
}

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (16 << 20);
  if (ok) {
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    ok = out.empty() || (ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size());
  }
  CloseHandle(f);
  // Strip UTF-8 BOM written by some editors.
  if (ok && out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB &&
      static_cast<unsigned char>(out[2]) == 0xBF) {
    out.erase(0, 3);
  }
  return ok;
}

bool WriteFileAtomic(const std::wstring& path, const std::string& data) {
  const std::wstring tmp = path + L".tmp";
  HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
  ok = FlushFileBuffers(f) && ok;
  CloseHandle(f);
  if (!ok) {
    DeleteFileW(tmp.c_str());
    return false;
  }
  if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE);
  return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool SetClipboardText(HWND owner, std::wstring_view text) {
  if (!OpenClipboard(owner)) return false;
  EmptyClipboard();
  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  bool ok = false;
  if (mem) {
    auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
    memcpy(dst, text.data(), text.size() * sizeof(wchar_t));
    dst[text.size()] = L'\0';
    GlobalUnlock(mem);
    ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
    if (!ok) GlobalFree(mem);
  }
  CloseClipboard();
  return ok;
}

std::wstring GetClipboardText(HWND owner) {
  std::wstring r;
  if (!OpenClipboard(owner)) return r;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
      r = p;
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return r;
}

bool IsProcessElevated(HANDLE process) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION e{};
  DWORD len = 0;
  const bool ok = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &len) && e.TokenIsElevated;
  CloseHandle(token);
  return ok;
}

std::vector<MonitorDesc> ListMonitors() {
  std::vector<MonitorDesc> out;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
          MonitorDesc d;
          d.rect = mi.rcMonitor;
          d.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
          d.device = mi.szDevice;
          reinterpret_cast<std::vector<MonitorDesc>*>(lp)->push_back(std::move(d));
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&out));
  std::stable_sort(out.begin(), out.end(), [](const MonitorDesc& a, const MonitorDesc& b) {
    if (a.primary != b.primary) return a.primary;
    if (a.rect.left != b.rect.left) return a.rect.left < b.rect.left;
    return a.rect.top < b.rect.top;
  });
  return out;
}

void Log(const wchar_t* component, const wchar_t* fmt, ...) {
  static std::mutex m;
  std::lock_guard lock(m);

  wchar_t msg[1024];
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
  va_end(ap);

  const std::wstring path = LogDir() + L"\\" + component + L".log";
  WIN32_FILE_ATTRIBUTE_DATA fa{};
  if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > (1u << 20)) {
    MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
  }

  SYSTEMTIME st;
  GetLocalTime(&st);
  const std::string text = Narrow(msg);
  char line[1600];
  int n = snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n", st.wYear, st.wMonth, st.wDay, st.wHour,
                   st.wMinute, st.wSecond, st.wMilliseconds, text.c_str());
  if (n < 0) return;
  if (n >= static_cast<int>(sizeof(line))) n = static_cast<int>(sizeof(line)) - 1;

  HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f != INVALID_HANDLE_VALUE) {
    DWORD w = 0;
    WriteFile(f, line, static_cast<DWORD>(n), &w, nullptr);
    CloseHandle(f);
  }
}

}  // namespace sl

namespace sl {

void RemoveUpdateLeftovers() {
  const std::wstring dir = ExeDir();
  WIN32_FIND_DATAW fd;
  const HANDLE h = FindFirstFileW((dir + L"\\*.exe.old").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

}  // namespace sl
