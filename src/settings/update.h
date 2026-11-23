#pragma once

// Update on request (Settings → About): the latest GitHub release of Fenderenko/Sightline.
// Nothing is checked on its own. The release zip replaces both executables next to this one.

#include <string>
#include <string_view>

namespace sl::update {

inline constexpr wchar_t kRepoPage[] = L"https://github.com/Fenderenko/Sightline";
inline constexpr wchar_t kReleasesPage[] = L"https://github.com/Fenderenko/Sightline/releases/latest";
inline constexpr wchar_t kAuthorPage[] = L"https://github.com/Fenderenko";

struct Release {
  std::string version;  // "0.3.0"
  std::wstring zipUrl;  // release asset Sightline-v<version>-win-x64.zip; empty when missing
};

// Product version of the running executable ("0.2.0"), from its VERSIONINFO.
std::string CurrentVersion();
// Numeric dotted comparison: <0, 0, >0.
int CompareVersions(std::string_view a, std::string_view b);

// Blocking, for a worker thread (up to ~20 s). False when GitHub cannot be reached or answers
// with something other than a release.
bool FetchLatest(Release& out);

// Blocking, for a worker thread. Downloads and unpacks the release zip into a fresh folder under
// %TEMP% and checks that both executables are there. `dir` receives that folder.
bool Download(const Release& r, std::wstring& dir);

// UI thread. Stops the core, swaps both executables (the old ones become *.old), starts the new
// core and a new settings window that waits for this process to exit. On failure everything is
// rolled back and false is returned.
bool Install(const std::wstring& dir);

}  // namespace sl::update
