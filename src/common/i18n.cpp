#include "common/i18n.h"

#include "common/i18n_tables.h"
#include "common/winutil.h"

#include <cwctype>
#include <mutex>
#include <string>
#include <unordered_map>

namespace sl {

namespace {

struct Entry {
  const char* key;
  const wchar_t* uk;
  const wchar_t* en;
};

// clang-format off
constexpr Entry kStrings[] = {
  // Tray
  {"tray.show", L"Показати приціл", L"Show crosshair"},
  {"tray.hide", L"Сховати приціл", L"Hide crosshair"},
  {"tray.crosshair", L"Приціл", L"Crosshair"},
  {"tray.settings", L"Налаштування…", L"Settings…"},
  {"tray.quit", L"Вийти", L"Quit"},
  // Chrome
  {"chrome.coreOnline", L"Ядро працює", L"Core online"},
  {"chrome.coreOffline", L"Ядро зупинене", L"Core offline"},
  {"chrome.overlay", L"Оверлей", L"Overlay"},
  {"chrome.tagline", L"Crosshair · Overlay", L"Crosshair · Overlay"},
  {"chrome.footLeft", L"External overlay", L"External overlay"},
  {"chrome.footRight", L"No hooks · No drivers", L"No hooks · No drivers"},
  {"btn.startCore", L"Запустити", L"Start"},
  // Navigation and titles
  {"nav.library", L"Бібліотека", L"Library"},
  {"nav.editor", L"Конструктор", L"Editor"},
  {"nav.hotkeys", L"Клавіші", L"Hotkeys"},
  {"nav.profiles", L"Профілі ігор", L"Game profiles"},
  {"nav.safety", L"Безпека", L"Safety"},
  {"nav.settings", L"Налаштування", L"Settings"},
  {"title.library", L"Бібліотека прицілів", L"Crosshair library"},
  {"title.editor", L"Конструктор", L"Editor"},
  {"title.hotkeys", L"Гарячі клавіші", L"Hotkeys"},
  {"title.profiles", L"Профілі ігор", L"Game profiles"},
  {"title.safety", L"Безпека та античіти", L"Safety and anti-cheat"},
  {"title.settings", L"Налаштування", L"Settings"},
  // Categories
  {"cat.all", L"Усі", L"All"},
  {"cat.fav", L"Обрані", L"Favorites"},
  {"cat.classic", L"Класичні", L"Classic"},
  {"cat.dot", L"Крапки", L"Dots"},
  {"cat.circle", L"Кола", L"Circles"},
  {"cat.tactical", L"Тактичні", L"Tactical"},
  {"cat.sniper", L"Снайперські", L"Sniper"},
  {"cat.access", L"Доступність", L"Accessibility"},
  {"cat.fun", L"Fun", L"Fun"},
  {"cat.mine", L"Мої", L"Mine"},
  // Library
  {"btn.new", L"+ Новий", L"+ New"},
  {"lib.newName", L"Мій приціл", L"My crosshair"},
  {"lib.active", L"Активний приціл", L"Active crosshair"},
  {"lib.modified", L"Змінений пресет", L"Modified preset"},
  {"lib.copySuffix", L" (копія)", L" (copy)"},
  {"lib.importCode", L"Імпорт за кодом", L"Import by code"},
  {"lbl.scale", L"Масштаб", L"Scale"},
  {"lbl.opacity", L"Непрозорість", L"Opacity"},
  {"lbl.globalOffsetX", L"Зсув X (усі)", L"Offset X (all)"},
  {"lbl.globalOffsetY", L"Зсув Y (усі)", L"Offset Y (all)"},
  {"btn.edit", L"Редагувати", L"Edit"},
  {"btn.duplicate", L"Дублювати", L"Duplicate"},
  {"btn.fav", L"☆ В обрані", L"☆ Favorite"},
  {"btn.unfav", L"★ Обраний", L"★ Favorited"},
  {"btn.delete", L"Видалити", L"Delete"},
  {"btn.reset", L"Скинути", L"Reset"},
  {"btn.copyCode", L"Копіювати код прицілу", L"Copy crosshair code"},
  {"btn.import", L"Імпорт", L"Import"},
  {"toast.deleted", L"Приціл видалено", L"Crosshair deleted"},
  {"toast.reset", L"Пресет повернуто до стандартного", L"Preset restored to default"},
  {"toast.codeCopied", L"Код скопійовано в буфер обміну", L"Code copied to clipboard"},
  {"toast.imported", L"Приціл імпортовано", L"Crosshair imported"},
  {"toast.badCode", L"Код не розпізнано", L"Code not recognised"},
  // Editor
  {"ed.name", L"Назва", L"Name"},
  {"ed.namePlaceholder", L"Назва прицілу", L"Crosshair name"},
  {"ed.layers", L"Шари", L"Layers"},
  {"ed.shape", L"Форма", L"Shape"},
  {"ed.color", L"Колір", L"Color"},
  {"ed.outline", L"Обводка", L"Outline"},
  {"ed.transform", L"Трансформація", L"Transform"},
  {"ed.bgGrid", L"Сітка", L"Grid"},
  {"ed.bgDark", L"Темний", L"Dark"},
  {"ed.bgLight", L"Світлий", L"Light"},
  {"ed.presetNote", L"Це вбудований пресет. Зміни зберігаються як модифікація, повернути стандартний вигляд можна кнопкою «Скинути» в бібліотеці.", L"This is a built-in preset. Edits are saved as a modification; use Reset in the library to restore it."},
  {"shape.cross", L"Хрест", L"Cross"},
  {"shape.dot", L"Крапка", L"Dot"},
  {"shape.ring", L"Кільце", L"Ring"},
  {"shape.box", L"Квадрат", L"Box"},
  {"shape.corners", L"Кути", L"Corners"},
  {"shape.chevron", L"Шеврон", L"Chevron"},
  {"shape.segments", L"Сегменти", L"Segments"},
  {"shape.plus", L"Плюс", L"Plus"},
  {"shape.x", L"Хрест X", L"X cross"},
  {"shape.t", L"T-подібний", L"T-shape"},
  {"shape.diamond", L"Ромб", L"Diamond"},
  {"shape.triangle", L"Трикутник", L"Triangle"},
  {"shape.line", L"Лінія", L"Line"},
  {"lbl.filled", L"Суцільна заливка", L"Solid fill"},
  {"lbl.halfLength", L"Довжина половини", L"Half length"},
  {"lbl.circumradius", L"Радіус", L"Radius"},
  // Features added in v0.1.1
  {"set.theme", L"Тема", L"Theme"},
  {"set.themeSystem", L"Як у системі", L"System"},
  {"set.themeLight", L"Світла", L"Light"},
  {"set.themeDark", L"Темна", L"Dark"},
  {"set.monitor", L"Монітор", L"Monitor"},
  {"set.monitorAuto", L"Де гра", L"Game's monitor"},
  {"set.monitorPrimary", L"Основний", L"Primary"},
  {"set.monitorN", L"Монітор", L"Monitor"},
  {"set.hideCursor", L"Ховати, коли видно курсор", L"Hide while the cursor is visible"},
  {"set.hideCursorHint", L"Меню, інвентар, карта: приціл зникає разом із появою курсора", L"Menus, inventory, map: the crosshair hides when the cursor appears"},
  {"hk.nudge", L"Зсув прицілу стрілками", L"Nudge with arrow keys"},
  {"hk.nudgeHint", L"Модифікатор + стрілка зсуває приціл на 1 px (у профілі гри — зсув профілю)", L"Modifier + arrow moves the crosshair by 1 px (inside a game profile, that profile's offset)"},
  {"hk.nudgeOff", L"Вимк.", L"Off"},
  {"hk.actSwitch", L"Інший приціл", L"Other crosshair"},
  {"hk.adsTarget", L"Приціл під час прицілювання", L"Crosshair while aiming"},
  {"lib.hotkey", L"Клавіша прицілу", L"Crosshair hotkey"},
  {"lib.hotkeyHint", L"Одразу вмикає цей приціл", L"Switches straight to this crosshair"},
  {"lib.importHint", L"SL1 · Valorant · CS2", L"SL1 · Valorant · CS2"},
  {"toast.importedValorant", L"Імпортовано приціл Valorant", L"Valorant crosshair imported"},
  {"toast.importedCs2", L"Імпортовано приціл CS2 (масштаб під ваш монітор)", L"CS2 crosshair imported (scaled to your monitor)"},
  {"btn.exportFile", L"Експорт бібліотеки", L"Export library"},
  {"btn.importFile", L"Імпорт з файлу", L"Import from file"},
  {"toast.exported", L"Бібліотеку збережено", L"Library saved"},
  {"toast.importedFile", L"Імпортовано прицілів:", L"Crosshairs imported:"},
  {"toast.importFailed", L"Файл не схожий на бібліотеку Sightline", L"The file is not a Sightline library"},
  {"btn.addLayer", L"+ Шар", L"+ Layer"},
  {"btn.removeLayer", L"− Шар", L"− Layer"},
  {"btn.up", L"↑ Вище", L"↑ Up"},
  {"btn.down", L"↓ Нижче", L"↓ Down"},
  {"lbl.length", L"Довжина", L"Length"},
  {"lbl.thickness", L"Товщина", L"Thickness"},
  {"lbl.gap", L"Проміжок", L"Gap"},
  {"lbl.rays", L"Промені", L"Rays"},
  {"lbl.roundCaps", L"Заокруглені кінці", L"Round caps"},
  {"lbl.size", L"Розмір", L"Size"},
  {"lbl.roundDot", L"Кругла крапка", L"Round dot"},
  {"lbl.radius", L"Радіус", L"Radius"},
  {"lbl.halfSize", L"Півширина", L"Half size"},
  {"lbl.distance", L"Відстань від центру", L"Distance from center"},
  {"lbl.arm", L"Довжина плеча", L"Arm length"},
  {"lbl.segments", L"Сегменти", L"Segments"},
  {"lbl.segmentGap", L"Проміжок між сегментами", L"Segment gap"},
  {"lbl.outlineOn", L"Обводка навколо форми", L"Outline around the shape"},
  {"lbl.outlineOpacity", L"Непрозорість обводки", L"Outline opacity"},
  {"lbl.rotation", L"Поворот", L"Rotation"},
  {"lbl.offsetX", L"Зсув X", L"Offset X"},
  {"lbl.offsetY", L"Зсув Y", L"Offset Y"},
  // Hotkeys
  {"hk.global", L"Глобальні", L"Global"},
  {"hk.pressKey", L"Натисніть клавішу… Esc — очистити", L"Press a key… Esc clears"},
  {"hk.unbound", L"Не призначено", L"Not bound"},
  {"hk.toggle", L"Показати / сховати приціл", L"Show / hide crosshair"},
  {"hk.toggleHint", L"Вмикає й вимикає оверлей у всіх іграх", L"Turns the overlay on and off everywhere"},
  {"hk.next", L"Наступний приціл", L"Next crosshair"},
  {"hk.nextHint", L"Перемикає по бібліотеці; у профілі гри змінює приціл профілю", L"Cycles the library; inside a game profile it changes that profile"},
  {"hk.prev", L"Попередній приціл", L"Previous crosshair"},
  {"hk.prevHint", L"Те саме у зворотному порядку", L"Same, in reverse order"},
  {"hk.settings", L"Відкрити налаштування", L"Open settings"},
  {"hk.settingsHint", L"Показує це вікно", L"Brings up this window"},
  {"hk.ads", L"Прицілювання (ADS)", L"Aim down sights (ADS)"},
  {"hk.adsKey", L"Клавіша прицілювання", L"Aim key"},
  {"hk.adsKeyHint", L"Та сама, що в грі. Можна ПКМ, СКМ, Mouse 4/5 або клавішу", L"Same as in the game. RMB, MMB, Mouse 4/5 or a key"},
  {"hk.adsMode", L"Режим, як у грі", L"Mode, as in the game"},
  {"hk.adsAction", L"Приціл під час прицілювання", L"Crosshair while aiming"},
  {"hk.hold", L"Утримання", L"Hold"},
  {"hk.toggleMode", L"Перемикання", L"Toggle"},
  {"hk.actNone", L"Без змін", L"No change"},
  {"hk.actHide", L"Ховати", L"Hide"},
  {"hk.actShow", L"Лише при ADS", L"Only in ADS"},
  {"hk.note", L"Глобальні клавіші реєструються через RegisterHotKey. Стан клавіші прицілювання опитується (125 Гц) лише тоді, коли її призначено. Ні хуків, ні ін'єкцій. Якщо гра запущена від адміністратора, запустіть Sightline теж від адміністратора, інакше Windows не передасть натискання.", L"Global keys use RegisterHotKey. The aim key is polled (125 Hz) only while it is bound. No hooks, no injection. If a game runs as administrator, run Sightline as administrator too, otherwise Windows will not pass the key presses."},
  {"toast.hotkeyMoved", L"Комбінацію перенесено з іншої дії:", L"Shortcut moved from another action:"},
  {"toast.mouseNotAllowed", L"Кнопки миші доступні лише для прицілювання", L"Mouse buttons are only allowed for aiming"},
  // Profiles
  {"prof.games", L"Профілі ігор", L"Game profiles"},
  {"prof.gamesHint", L"Коли гра активна, Sightline автоматично вмикає її приціл і зсув.", L"When the game is in front, Sightline switches to its crosshair and offset."},
  {"prof.empty", L"Список порожній", L"The list is empty"},
  {"prof.noRunning", L"Немає вікон програм", L"No application windows"},
  {"btn.add", L"Додати", L"Add"},
  {"btn.fromRunning", L"З запущених…", L"From running…"},
  {"prof.filter", L"Де показувати приціл", L"Where the crosshair shows"},
  {"prof.fmAll", L"Всюди", L"Everywhere"},
  {"prof.fmWhite", L"Лише мої ігри", L"Only my games"},
  {"prof.fmBlack", L"Крім списку", L"Except listed"},
  {"prof.fmAllHint", L"Приціл показується над будь-яким вікном.", L"The crosshair shows over any window."},
  {"prof.fmWhiteHint", L"Лише в іграх із профілів вище та з цього списку. У браузері й інших програмах прицілу немає.", L"Only in the games from the profiles above and from this list. Browsers and other apps get no crosshair."},
  {"prof.fmWhiteEmpty", L"Ігор ще немає: додайте гру вище, інакше приціл не з'явиться ніде.", L"No games yet: add one above, otherwise the crosshair shows nowhere."},
  {"prof.fmBlackHint", L"Приціл показується скрізь, окрім програм зі списку.", L"The crosshair shows everywhere except listed apps."},
  {"prof.safe", L"Competitive-safe", L"Competitive-safe"},
  {"prof.safeHint", L"Для цих exe оверлей вимикається повністю, разом з опитуванням клавіш. Додайте сюди турнірні клієнти та ігри, правила яких забороняють сторонні приціли.", L"For these executables the overlay is fully disabled, including key polling. Add tournament clients and games whose rules forbid third-party crosshairs."},
  // Safety
  {"safe.doesTitle", L"Що робить Sightline", L"What Sightline does"},
  {"safe.does1", L"Малює приціл у власному прозорому вікні поверх екрана. Гра про це вікно нічого не знає.", L"Draws the crosshair in its own transparent window on top of the screen. The game is unaware of it."},
  {"safe.does2", L"Дізнається лише ім'я exe активного вікна, щоб увімкнути профіль (PROCESS_QUERY_LIMITED_INFORMATION).", L"Reads only the exe name of the active window to apply a profile (PROCESS_QUERY_LIMITED_INFORMATION)."},
  {"safe.does3", L"Слухає гарячі клавіші через стандартний RegisterHotKey, а клавішу прицілювання опитує лише якщо її призначено.", L"Listens to hotkeys via standard RegisterHotKey and polls the aim key only when it is bound."},
  {"safe.does4", L"Перемальовує приціл тільки коли він змінюється. У грі програма майже нічого не споживає.", L"Redraws only when the crosshair changes, so it costs next to nothing in game."},
  {"safe.notTitle", L"Чого не робить", L"What it never does"},
  {"safe.not1", L"Не впроваджує DLL і не перехоплює DirectX, OpenGL чи Vulkan.", L"No DLL injection, no DirectX, OpenGL or Vulkan hooks."},
  {"safe.not2", L"Не читає й не змінює пам'ять чи файли гри.", L"Does not read or modify game memory or files."},
  {"safe.not3", L"Не аналізує пікселі екрана і не надсилає ввід (без макросів і aim assist).", L"No screen pixel analysis and no input injection (no macros, no aim assist)."},
  {"safe.not4", L"Не встановлює драйверів ядра, зокрема WinRing0.", L"Installs no kernel drivers, WinRing0 included."},
  {"safe.not5", L"Не ховає свій процес: стабільне ім'я, без пакувальників і обфускації.", L"Does not hide its process: stable name, no packers or obfuscation."},
  {"safe.warnTitle", L"Чесно про ризики", L"Honest note on risk"},
  {"safe.warn", L"Жоден сторонній оверлей не має офіційної гарантії від розробників ігор. Деякі турнірні платформи та ліги прямо забороняють сторонні приціли. Перевірте правила гри та додайте її в Competitive-safe, якщо сумніваєтеся.", L"No third-party overlay has an official guarantee from game developers. Some tournament platforms and leagues explicitly forbid third-party crosshairs. Check the game's rules and add it to Competitive-safe if in doubt."},
  {"safe.status", L"Стан ядра", L"Core status"},
  {"safe.coreRunning", L"Ядро працює", L"Core running"},
  {"safe.coreStopped", L"Ядро не запущене", L"Core is not running"},
  {"safe.elevated", L"права адміністратора", L"administrator"},
  {"safe.normal", L"звичайні права", L"standard user"},
  // Settings
  {"set.language", L"Мова інтерфейсу", L"Interface language"},
  {"set.position", L"Положення прицілу", L"Crosshair position"},
  {"set.centerMonitor", L"Центр монітора", L"Monitor center"},
  {"set.centerWindow", L"Центр вікна гри", L"Game window center"},
  {"set.centerHint", L"«Центр вікна» корисний для ігор у віконному режимі, де вікно не по центру екрана.", L"Window center helps with windowed games that are not centered on the screen."},
  {"set.behaviour", L"Поведінка", L"Behaviour"},
  {"set.autostart", L"Запускати з Windows", L"Start with Windows"},
  {"set.autostartHint", L"Ядро стартує після входу в систему", L"Core starts after you sign in"},
  {"set.sleep", L"Сон до запиту", L"Sleep until requested"},
  {"set.sleepHint", L"Після старту приціл прихований до хоткея або запуску гри з профілем", L"After start the crosshair stays hidden until a hotkey or a profiled game"},
  {"set.hideDesktop", L"Ховати на робочому столі", L"Hide on the desktop"},
  {"set.hideDesktopHint", L"Робочий стіл, панель завдань, «Пуск» і Alt+Tab", L"The crosshair hides while the desktop is active"},
  {"set.hideOverApp", L"Ховати над вікном Sightline", L"Hide over the Sightline window"},
  {"set.hideOverAppHint", L"Вимкніть, щоб бачити зміни в грі наживо", L"Turn off to see edits live in game"},
  {"set.capture", L"Не показувати на стрімі та записі", L"Hide from streams and recordings"},
  {"set.captureHint", L"Приціл бачите лише ви (WDA_EXCLUDEFROMCAPTURE)", L"Only you see the crosshair (WDA_EXCLUDEFROMCAPTURE)"},
  {"set.files",L"Файли", L"Files"},
  {"btn.openFolder", L"Відкрити папку конфігів", L"Open config folder"},
  {"btn.resetAll", L"Скинути все", L"Reset everything"},
  {"btn.confirmReset", L"Точно скинути?", L"Really reset?"},
  {"toast.resetAll", L"Налаштування скинуто", L"Settings reset"},
  {"set.onlyFullscreen", L"Лише в повноекранних іграх", L"Only in fullscreen games"},
  {"set.onlyFullscreenHint", L"Приціл видно, тільки коли активне вікно займає весь монітор (borderless)", L"Visible only while the active window covers the whole monitor (borderless)"},
  {"prof.exePlaceholder", L"назва .exe гри", L"game .exe name"},
  {"toast.pickExe", L"Введіть назву exe або оберіть гру зі списку запущених нижче", L"Type an exe name or pick a game from the running list below"},
  {"toast.needModifier", L"Додайте Ctrl, Alt або Shift: інакше клавіша перестане працювати в усіх програмах", L"Add Ctrl, Alt or Shift: otherwise the key stops working in every app"},
  // ── UI agent strings (settings redesign) ── keep below this line
  {"chrome.tagline2", L"Чистий приціл поверх будь-якої гри", L"A clean crosshair over any game"},
  {"chrome.sections", L"Розділи", L"Sections"},
  {"foot.crosshair", L"Приціл", L"Crosshair"},
  {"foot.monitor", L"Монітор", L"Monitor"},
  {"foot.offset", L"Зсув", L"Offset"},
  {"lib.presets", L"Пресети", L"Presets"},
  {"lib.tagActive", L"Активний", L"Active"},
  {"lib.tagModified", L"Змінено", L"Modified"},
  {"lib.newCell", L"Новий приціл", L"New crosshair"},
  {"lib.params", L"Параметри", L"Parameters"},
  {"lib.kindPreset", L"Пресет", L"Preset"},
  {"lib.kindUser", L"Власний", L"Custom"},
  {"lib.exchange", L"Обмін", L"Sharing"},
  {"btn.newShort", L"Новий", L"New"},
  {"btn.favShort", L"В обрані", L"Favorite"},
  {"btn.unfavShort", L"Обраний", L"Favorited"},
  {"btn.copyCodeShort", L"Копіювати код", L"Copy code"},
  {"btn.layerAdd", L"Шар", L"Layer"},
  {"btn.layerRemove", L"Прибрати", L"Remove"},
  {"btn.layerUp", L"Вище", L"Up"},
  {"btn.layerDown", L"Нижче", L"Down"},
  {"hk.pressKeyShort", L"Натисніть клавішу…", L"Press a key…"},
  {"hk.escClears", L"Esc — очистити", L"Esc clears"},
  {"ed.canvas", L"Полотно", L"Canvas"},
  {"set.interface", L"Інтерфейс", L"Interface"},
  {"set.monitorHint", L"На якому моніторі малювати приціл, якщо їх кілька", L"Which monitor shows the crosshair when there are several"},
  {"prof.colExe", L"Програма", L"Executable"},
  {"prof.colCrosshair", L"Приціл", L"Crosshair"},
  {"prof.colOffset", L"Зсув", L"Offset"},
  {"toast.saveFailed", L"Не вдалося зберегти файл", L"Could not save the file"},
  {"lbl.distanceShort", L"Відстань", L"Distance"},
  {"ed.position", L"Позиція", L"Position"},
  {"set.centerMode", L"Центрування", L"Centering"},
  {"lib.empty", L"Тут поки порожньо", L"Nothing here yet"},
  {"lib.search", L"Пошук прицілів", L"Search crosshairs"},
  {"lib.noResults", L"Нічого не знайдено", L"No matches"},
  {"cat.shapes", L"Фігури", L"Shapes"},
  {"menu.fav", L"В обрані", L"Add to favorites"},
  {"menu.unfav", L"З обраних", L"Remove from favorites"},
  {"toast.layerLimit", L"Не більше 8 шарів", L"8 layers at most"},
  {"lbl.splitV", L"Окрема вертикальна довжина", L"Separate vertical length"},
  {"lbl.lengthH", L"По горизонталі", L"Horizontal"},
  {"lbl.lengthV", L"По вертикалі", L"Vertical"},
  {"lbl.centerDot", L"Точка в центрі", L"Centre dot"},
  {"ed.save", L"Зберегти", L"Save"},
  {"ed.saveMine", L"Зберегти в «Мої»", L"Save to “Mine”"},
  {"ed.saveAsNew", L"Зберегти як новий", L"Save as new"},
  {"ed.discard", L"Скасувати зміни", L"Discard changes"},
  {"ed.unsaved", L"Незбережені зміни", L"Unsaved changes"},
  {"ed.saved", L"Збережено", L"Saved"},
  {"ed.presetLine", L"Вбудований пресет · зміни збережуться як ваша копія", L"Built-in preset · changes are saved as your copy"},
  {"ed.mineSuffix", L" (моя)", L" (mine)"},
  {"ed.undoTip", L"Скасувати дію (Ctrl+Z)", L"Undo (Ctrl+Z)"},
  {"ed.redoTip", L"Повторити (Ctrl+Y)", L"Redo (Ctrl+Y)"},
  {"ed.layerAdd", L"Додати шар", L"Add layer"},
  {"ed.layerDup", L"Дублювати шар (Ctrl+D)", L"Duplicate layer (Ctrl+D)"},
  {"ed.layerDelete", L"Видалити шар (Delete)", L"Delete layer (Delete)"},
  {"ed.layerUp", L"Шар вище", L"Move layer up"},
  {"ed.layerDown", L"Шар нижче", L"Move layer down"},
  {"ed.layerHide", L"Сховати шар", L"Hide layer"},
  {"ed.layerShow", L"Показати шар", L"Show layer"},
  {"ed.lastLayer", L"Останній шар не можна видалити", L"The last layer can't be deleted"},
  {"toast.saved", L"Зміни збережено", L"Changes saved"},
  {"toast.presetCopy", L"Пресет не змінено — збережено вашу копію в «Мої»", L"Preset unchanged — your copy is saved in “Mine”"},
  {"toast.savedNew", L"Збережено як новий приціл", L"Saved as a new crosshair"},
  {"toast.discarded", L"Зміни скасовано", L"Changes discarded"},
  {"dlg.saveTitlePre", L"Зберегти зміни в «", L"Save changes to “"},
  {"dlg.saveTitlePost", L"»?", L"”?"},
  {"dlg.body", L"У Конструкторі є незбережені зміни. Без збереження вони зникнуть.", L"The editor has unsaved changes. They are lost unless you save them."},
  {"dlg.bodyClose", L"Вікно закривається, а в Конструкторі є незбережені зміни.", L"The window is closing and the editor has unsaved changes."},
  {"dlg.bodyPreset", L"Пресет не зміниться: буде створено вашу копію в «Мої».", L"The preset stays as it is: your copy is created in “Mine”."},
  {"dlg.save", L"Зберегти", L"Save"},
  {"dlg.dontSave", L"Не зберігати", L"Don't save"},
  {"dlg.cancel", L"Скасувати", L"Cancel"},
  {"lib.presetReadOnly", L"Пресет лише для читання: змініть його в Конструкторі, і збережеться ваша копія", L"Presets are read-only: change it in the editor and your copy is saved"},
  {"lib.draftPending", L"Незбережені зміни в Конструкторі", L"Unsaved changes in the editor"},
  {"chrome.startCoreTip", L"Запустити sightline.exe з папки програми", L"Launch sightline.exe from the app folder"},
  {"ed.fill", L"Заливка", L"Fill"},
  {"lbl.blur", L"Розмиття", L"Blur"},
  {"ed.lastRay", L"Має лишитися хоча б один промінь", L"At least one ray must stay on"},
  {"ed.layerCopy", L"Копіювати шар (Ctrl+C)", L"Copy layer (Ctrl+C)"},
  {"ed.layerPaste", L"Вставити шар (Ctrl+V)", L"Paste layer (Ctrl+V)"},
  {"ed.stylePaste", L"Вставити лише стиль (Ctrl+Shift+V)", L"Paste style only (Ctrl+Shift+V)"},
  {"toast.layerCopied", L"Шар скопійовано", L"Layer copied"},
  {"toast.stylePasted", L"Стиль застосовано до шару", L"Style applied to the layer"},
  {"toast.exeExists", L"Ця програма вже у списку", L"This program is already listed"},
  {"dlg.exeFilter", L"Програми (*.exe)", L"Programs (*.exe)"},
  {"dlg.pickExe", L"Оберіть exe-файл гри", L"Choose the game's exe file"},
  {"btn.browseExe", L"Огляд…", L"Browse…"},
  {"btn.running", L"З запущених", L"From running"},
  {"prof.exeTyped", L"або назва.exe + Enter", L"or name.exe + Enter"},
  {"run.search", L"Пошук програм", L"Search programs"},
  {"run.background", L"Фонові процеси", L"Background processes"},
  {"set.configFolder", L"Папка налаштувань", L"Settings folder"},
  {"set.logsFolder", L"Папка журналів", L"Logs folder"},
  {"btn.openShort", L"Відкрити", L"Open"},
  // About and updates
  {"set.about", L"Про програму", L"About"},
  {"about.version", L"Версія %s", L"Version %s"},
  {"about.check", L"Перевірити оновлення", L"Check for updates"},
  {"about.checking", L"Перевіряю…", L"Checking…"},
  {"about.upToDate", L"У вас остання версія", L"You have the latest version"},
  {"about.available", L"Доступна версія %s", L"Version %s is available"},
  {"about.update", L"Оновити до %s", L"Update to %s"},
  {"about.download", L"Завантажити", L"Download"},
  {"about.downloading", L"Завантажую…", L"Downloading…"},
  {"about.checkFailed", L"Не вдалося зв'язатися з GitHub", L"Couldn't reach GitHub"},
  {"about.failed", L"Не вдалося оновити. Завантажте нову версію зі сторінки релізів", L"The update failed. Download the new version from the releases page"},
  {"about.saveFirst", L"Спершу збережіть або скасуйте зміни в Конструкторі", L"Save or discard the editor changes first"},
  {"about.desc", L"Власний приціл поверх ігор у режимі вікна без рамки. Нічого не впроваджується в гру: жодних хуків, драйверів чи читання пам'яті.", L"Your own crosshair over games in borderless windowed mode. Nothing is injected into the game: no hooks, no drivers, no memory reading."},
  {"about.github", L"Sightline на GitHub", L"Sightline on GitHub"},
  {"about.githubSub", L"Вихідний код, релізи й повідомлення про помилки", L"Source code, releases and bug reports"},
  {"about.authorSub", L"Автор · інші проєкти", L"Author · other projects"},
};
// clang-format on

constexpr LanguageInfo kLanguages[] = {
    {"en", L"English"},  {"uk", L"Українська"}, {"de", L"Deutsch"},
    {"fr", L"Français"}, {"es", L"Español"},    {"pt", L"Português (Brasil)"},
    {"pl", L"Polski"},   {"it", L"Italiano"},   {"tr", L"Türkçe"},
};

using Table = std::span<const Translation> (*)();
struct Extra {
  const char* code;
  Table table;
};
constexpr Extra kExtra[] = {
    {"de", &TranslationsDe}, {"fr", &TranslationsFr}, {"es", &TranslationsEs}, {"pt", &TranslationsPt},
    {"pl", &TranslationsPl}, {"it", &TranslationsIt}, {"tr", &TranslationsTr},
};

Table FindExtra(std::string_view code) {
  for (const Extra& e : kExtra) {
    if (code == e.code) return e.table;
  }
  return nullptr;
}

const std::unordered_map<std::string_view, const Entry*>& Index() {
  static const std::unordered_map<std::string_view, const Entry*> index = [] {
    std::unordered_map<std::string_view, const Entry*> m;
    for (const Entry& e : kStrings) m.emplace(e.key, &e);
    return m;
  }();
  return index;
}

std::unordered_map<std::string_view, const wchar_t*> TableIndex(Table table) {
  std::unordered_map<std::string_view, const wchar_t*> m;
  if (table) {
    for (const Translation& t : table()) m.emplace(t.key, t.text);
  }
  return m;
}

// printf conversions of a text in order ("%s%d"), so translations can be checked against English.
std::wstring Specifiers(const wchar_t* s) {
  std::wstring out;
  for (; *s; ++s) {
    if (*s != L'%') continue;
    ++s;
    if (!*s) break;
    if (*s == L'%') continue;
    while (*s && !iswalpha(*s)) ++s;  // flags, width, precision
    if (*s == L'z' || *s == L'l' || *s == L'h') out += *s++;
    if (*s) out += *s;
    if (!*s) break;
  }
  return out;
}

std::mutex g_mutex;
std::string g_code = "en";
std::unordered_map<std::string_view, const wchar_t*> g_extra;  // current extra language only

}  // namespace

std::span<const LanguageInfo> Languages() { return kLanguages; }

void SetLanguage(std::string_view lang) {
  std::lock_guard lock(g_mutex);
  bool known = false;
  for (const LanguageInfo& l : kLanguages) known |= lang == l.code;
  const std::string code = known ? std::string(lang) : "en";
  if (code == g_code) return;
  g_code = code;
  g_extra = TableIndex(FindExtra(g_code));
}

std::string_view Language() {
  std::lock_guard lock(g_mutex);
  return g_code;
}

const wchar_t* T(std::string_view key) {
  const auto& index = Index();
  const auto it = index.find(key);
  if (it != index.end()) {
    std::lock_guard lock(g_mutex);
    if (g_code == "uk") return it->second->uk;
    if (!g_extra.empty()) {
      const auto tr = g_extra.find(key);
      if (tr != g_extra.end()) return tr->second;
    }
    return it->second->en;
  }
  // Missing key: return the key itself (cached so the pointer stays valid).
  static std::mutex m;
  static std::unordered_map<std::string, std::wstring> missing;
  std::lock_guard lock(m);
  auto& w = missing[std::string(key)];
  if (w.empty()) w = Widen(key);
  return w.c_str();
}

int MissingTranslations(std::string_view lang, int* badFormat) {
  int missing = 0, bad = 0;
  const Table table = FindExtra(lang);
  const auto tr = TableIndex(table);
  for (const Entry& e : kStrings) {
    const wchar_t* text = lang == "uk" ? e.uk : nullptr;
    if (lang == "en") text = e.en;
    if (table) {
      const auto it = tr.find(e.key);
      if (it != tr.end()) text = it->second;
    }
    if (!text) {
      ++missing;
      continue;
    }
    if (Specifiers(text) != Specifiers(e.en)) ++bad;
  }
  if (badFormat) *badFormat = bad;
  return missing;
}

}  // namespace sl
