#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>

#include <string>
#include <string_view>
#include <vector>

namespace sl {

using Microsoft::WRL::ComPtr;

std::wstring Widen(std::string_view s);
std::string Narrow(std::wstring_view s);
std::wstring Lower(std::wstring_view s);
std::wstring Upper(std::wstring_view s);
std::wstring FileNameOf(std::wstring_view path);

// Directory of the running executable, without trailing separator.
std::wstring ExeDir();
// %APPDATA%\Sightline (created on first use).
std::wstring ConfigDir();
// %LOCALAPPDATA%\Sightline\logs (created on first use).
std::wstring LogDir();

// Lowercase image file name of a process. Opens the process with
// PROCESS_QUERY_LIMITED_INFORMATION only; never reads its memory.
std::wstring ProcessExeName(DWORD pid);
std::wstring WindowExeName(HWND hwnd);

bool ReadFileUtf8(const std::wstring& path, std::string& out);
// Writes to <path>.tmp, keeps the previous file as <path>.bak, then replaces <path>.
bool WriteFileAtomic(const std::wstring& path, const std::string& data);

bool SetClipboardText(HWND owner, std::wstring_view text);
std::wstring GetClipboardText(HWND owner);

bool IsProcessElevated(HANDLE process);

struct MonitorDesc {
  RECT rect{};
  bool primary = false;
  std::wstring device;  // \\.\DISPLAY1
};
// Monitors in a stable order: primary first, then left-to-right, top-to-bottom.
std::vector<MonitorDesc> ListMonitors();

// Deletes *.exe.old next to the executable, left behind by an update (best effort).
void RemoveUpdateLeftovers();

// Appends a line to <LogDir>\<component>.log; rotates at 1 MB.
void Log(const wchar_t* component, const wchar_t* fmt, ...);

}  // namespace sl
