#pragma once

#include "common/winutil.h"

#include <string>

namespace sl::ipc {

inline constexpr wchar_t kCoreClass[] = L"Sightline.Core";
inline constexpr wchar_t kSettingsClass[] = L"Sightline.Settings";
inline constexpr wchar_t kOverlayClass[] = L"Sightline.Overlay";
inline constexpr wchar_t kCoreMutex[] = L"Local\\Sightline.Core.Instance";
inline constexpr wchar_t kSettingsMutex[] = L"Local\\Sightline.Settings.Instance";
inline constexpr wchar_t kCoreExe[] = L"sightline.exe";
inline constexpr wchar_t kSettingsExe[] = L"sightline-settings.exe";

// WM_COPYDATA ids.
enum : ULONG_PTR {
  kApplyConfig = 1,     // Settings → Core: full config JSON (UTF-8), apply without saving.
  kSuspendHotkeys = 2,  // Settings → Core: unregister global hotkeys while a key is being bound.
  kResumeHotkeys = 3,   // Settings → Core: register them again.
  kPreviewCrosshair = 4,  // Settings → Core: crosshair JSON shown temporarily (unsaved draft); empty payload ends it.
  kConfigFromCore = 10  // Core → Settings: full config JSON after a hotkey changed state.
};

// Posted messages.
inline constexpr UINT kMsgOpenSettings = WM_APP + 20;   // → Core: open or focus settings.
inline constexpr UINT kMsgActivate = WM_APP + 21;       // → Settings: restore and focus.

inline bool Send(HWND to, HWND from, ULONG_PTR id, const std::string& data) {
  if (!to) return false;
  COPYDATASTRUCT cds{id, static_cast<DWORD>(data.size()), const_cast<char*>(data.data())};
  DWORD_PTR result = 0;
  return SendMessageTimeoutW(to, WM_COPYDATA, reinterpret_cast<WPARAM>(from), reinterpret_cast<LPARAM>(&cds),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &result) != 0;
}

inline HWND CoreWindow() { return FindWindowW(kCoreClass, nullptr); }
inline HWND SettingsWindow() { return FindWindowW(kSettingsClass, nullptr); }

}  // namespace sl::ipc
