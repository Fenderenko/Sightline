#pragma once

#include "common/json.h"
#include "common/winutil.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sl {

// 2: nudge hotkeys became opt-in (global arrow-key hotkeys clash with drivers and text editing).
inline constexpr int kConfigVersion = 2;

struct Color {
  float r = 1, g = 1, b = 1, a = 1;
  bool operator==(const Color&) const = default;
};
Color ColorFromHex(std::string_view hex, bool* ok = nullptr);
std::string ColorToHex(const Color& c);

// Shape semantics (all sizes in pixels at scale 1):
//  Cross    - rays (length, thickness) separated from the center by `gap`; ray mask in `rays`.
//  Plus     - solid "+" with a filled center: arms of `length` from the center block, no gap.
//  XCross   - Cross turned 45° on top of `rotation`.
//  TShape   - Cross with the top ray removed (`rays` ignored).
//  Dot      - square or round (`round`) dot of diameter `size`.
//  Ring     - circle of radius `size`, stroke `thickness`.
//  Box      - square of half-size `size`; outline of `thickness` or solid when `filled`.
//  Diamond  - square turned 45°, half-diagonal `size`; outline or `filled`.
//  Triangle - equilateral, pointing up, circumradius `size`; outline or `filled`.
//  Corners  - four L-brackets at half-size `gap`, arm `length`.
//  Chevron  - "^" below the center at `gap`, arms of `length`.
//  Segments - ring of radius `size` split into `segments` arcs with `segmentGap` degrees between.
//  Line     - horizontal bar: two halves of `length` separated by `gap` (gap 0 = continuous line).
enum class Shape : int {
  Cross, Plus, XCross, TShape, Dot, Ring, Box, Diamond, Triangle, Corners, Chevron, Segments, Line, Count
};
const char* ShapeKey(Shape s);
Shape ShapeFromKey(std::string_view key);

struct Hotkey {
  UINT mods = 0;  // MOD_* flags
  UINT vk = 0;
  bool Valid() const { return vk != 0; }
  bool operator==(const Hotkey&) const = default;
};

// Ray bits for Shape::Cross, clockwise starting at the top.
enum RayBits : uint8_t { kRayUp = 1, kRayRight = 2, kRayDown = 4, kRayLeft = 8, kRayAll = 15 };

// All distances are in screen pixels at scale 1.
struct Layer {
  Shape shape = Shape::Cross;
  float length = 6;       // Cross: ray length. Corners / Chevron: arm length.
  float lengthV = 0;      // Cross / X / T: vertical ray length; 0 = same as `length`.
  float thickness = 2;    // Line / ring stroke width.
  float gap = 3;          // Cross / Chevron: distance from center. Corners: half-size of the box.
  float size = 2;         // Dot: diameter. Ring / Segments: radius. Box: half-size.
  int rays = kRayAll;     // Cross only.
  int segments = 4;       // Segments only.
  float segmentGap = 30;  // Segments only, degrees.
  float rotation = 0;     // Degrees, clockwise.
  bool round = false;     // Dot: circle instead of square. Cross: round caps.
  bool filled = false;    // Box / Diamond / Triangle: solid instead of outline.
  Color fill{1, 1, 1, 1};
  float opacity = 1;
  bool outline = true;
  float outlineThickness = 1;
  Color outlineColor{0, 0, 0, 1};
  float outlineOpacity = 1;
  float blur = 0;         // Gaussian softening of the fill, px (0..kMaxBlur).
  float outlineBlur = 0;  // Gaussian softening of the outline only (soft shadow / glow), px.
  float offsetX = 0, offsetY = 0;
  bool hidden = false;
};
inline constexpr float kMaxBlur = 8;

struct Crosshair {
  std::string id;
  std::string name;
  std::string category;  // classic | dot | circle | tactical | sniper | access | fun | mine
  bool builtin = false;
  bool favorite = false;
  float scale = 1;
  float opacity = 1;
  std::vector<Layer> layers;
  Hotkey hotkey;  // switches to this crosshair; unbound by default
};
std::wstring KeyName(UINT vk);
std::wstring HotkeyToText(const Hotkey& h);

enum class AdsMode : int { Hold, Toggle };
enum class AdsAction : int { None, Hide, ShowOnly, Switch };  // Switch: show adsCrosshairId while aiming
enum class FilterMode : int { All, Whitelist, Blacklist };
enum class CenterMode : int { Monitor, Window };

struct GameProfile {
  std::wstring exe;  // lowercase file name
  std::string crosshairId;
  int offsetX = 0, offsetY = 0;
};

struct Settings {
  std::string language = "en";   // interface language code, see Languages()
  std::string theme = "dark";    // system | light | dark (settings window only)
  int monitor = -1;              // -1: monitor of the game window; 0: primary; n >= 1: ListMonitors()[n - 1]
  UINT nudgeMods = 0;            // modifiers + arrow keys move the crosshair by 1 px; 0 = off
  bool hideWhenCursor = false;   // hide while the mouse cursor is visible (game menus, inventory)
  bool enabled = true;
  bool startHidden = false;
  bool autostart = false;
  CenterMode centerMode = CenterMode::Monitor;
  bool hideOnDesktop = false;   // desktop, taskbar, Start, Alt+Tab and other shell surfaces
  bool onlyFullscreen = false;  // show only while the active window covers its whole monitor (borderless games)
  bool hideOverSettings = false;
  bool excludeFromCapture = false;
  int offsetX = 0, offsetY = 0;

  Hotkey hkToggle{MOD_CONTROL | MOD_SHIFT, VK_F9};
  Hotkey hkNext{MOD_CONTROL | MOD_SHIFT, VK_F8};
  Hotkey hkPrev{MOD_CONTROL | MOD_SHIFT, VK_F7};
  Hotkey hkSettings{MOD_CONTROL | MOD_SHIFT, VK_F11};

  UINT adsKey = 0;  // 0 = unbound; may be a mouse button VK
  AdsMode adsMode = AdsMode::Hold;
  AdsAction adsAction = AdsAction::Hide;
  std::string adsCrosshairId = "sniper.dot_lines";  // used by AdsAction::Switch

  FilterMode filterMode = FilterMode::All;
  std::vector<std::wstring> filterList;
  std::vector<std::wstring> safeList;  // overlay fully disabled for these exes
  std::vector<GameProfile> profiles;
};

struct Config {
  int version = kConfigVersion;
  std::string activeId = "classic.cross";
  Settings settings;
  // User crosshairs plus modified presets (same id as the preset they override).
  std::vector<Crosshair> crosshairs;
};

const std::vector<Crosshair>& BuiltinPresets();
const Crosshair* FindBuiltin(std::string_view id);

// Presets (with user overrides applied) followed by user crosshairs.
std::vector<const Crosshair*> Library(const Config& cfg);
const Crosshair* FindCrosshair(const Config& cfg, std::string_view id);
const Crosshair& ResolveCrosshair(const Config& cfg, std::string_view id);  // falls back to the first preset
// True only when the stored copy's content differs from the built-in (favourite / hotkey alone don't count).
bool IsModifiedPreset(const Config& cfg, std::string_view id);
// Compares the look of two crosshairs, ignoring id, category, favourite and hotkey.
bool SameContent(const Crosshair& a, const Crosshair& b);
// Returns the stored copy, creating an override when `id` is a built-in preset.
Crosshair& EditableCrosshair(Config& cfg, std::string_view id);
void RemoveUserCrosshair(Config& cfg, std::string_view id);
std::string NewCrosshairId();

json::Value ToJson(const Layer& l);
json::Value ToJson(const Crosshair& c);
json::Value ToJson(const Config& c);
void FromJson(const json::Value& v, Layer& l);
void FromJson(const json::Value& v, Crosshair& c);
void FromJson(const json::Value& v, Config& c);

std::string ConfigToString(const Config& c, bool pretty);
bool ConfigFromString(std::string_view text, Config& c);
std::string CrosshairFingerprint(const Crosshair& c);

std::wstring ConfigPath();
bool LoadConfig(Config& c);
bool SaveConfig(const Config& c);

// "SL1." + base64url(compact JSON). No server involved.
std::string EncodeShareCode(const Crosshair& c);
bool DecodeShareCode(std::string_view code, Crosshair& out);

// Accepts a Sightline code (SL1.), a Valorant profile code ("0;P;c;5;…") or a CS2 share code
// ("CSGO-xxxxx-…" legacy or "CS…" current). The result gets a new id and category "mine".
enum class CodeSource { None, Sightline, Valorant, Cs2 };
CodeSource ImportCrosshairCode(std::string_view code, Crosshair& out);

// Library file: JSON array of user crosshairs and modified presets.
std::string ExportLibrary(const Config& cfg);
// Adds crosshairs from an exported library (new ids for user ones, presets overwrite their override).
// Returns the number imported, or -1 when the text is not a library file.
int ImportLibrary(std::string_view text, Config& cfg);

}  // namespace sl
