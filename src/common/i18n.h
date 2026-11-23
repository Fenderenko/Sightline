#pragma once

#include <span>
#include <string_view>

namespace sl {

struct LanguageInfo {
  const char* code;      // config value: "en", "uk", "de", …
  const wchar_t* name;   // native name shown in the language picker
};
// Supported interface languages, English first.
std::span<const LanguageInfo> Languages();

// Unknown codes fall back to English.
void SetLanguage(std::string_view lang);
std::string_view Language();

// Localised string for `key`. A key missing in the current language falls back to English;
// a key missing everywhere returns the key itself so gaps are visible in the UI.
const wchar_t* T(std::string_view key);

// Self-test: number of keys without a translation in `lang`; `badFormat` receives the number of
// translations whose printf specifiers differ from the English text.
int MissingTranslations(std::string_view lang, int* badFormat);

}  // namespace sl
