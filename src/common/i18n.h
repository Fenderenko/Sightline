#pragma once

#include <string_view>

namespace sl {

// "uk" or "en". Unknown values fall back to "uk".
void SetLanguage(std::string_view lang);
std::string_view Language();

// Localised string for `key`. Missing keys return the key itself so gaps are visible in the UI.
const wchar_t* T(std::string_view key);

}  // namespace sl
