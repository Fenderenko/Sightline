#pragma once

// Translations beyond the built-in Ukrainian / English table (src/common/i18n.cpp).
// One file per language: src/common/i18n_<code>.cpp.

#include <span>

namespace sl {

struct Translation {
  const char* key;
  const wchar_t* text;
};

std::span<const Translation> TranslationsDe();
std::span<const Translation> TranslationsFr();
std::span<const Translation> TranslationsEs();
std::span<const Translation> TranslationsPt();
std::span<const Translation> TranslationsPl();
std::span<const Translation> TranslationsIt();
std::span<const Translation> TranslationsTr();

}  // namespace sl
