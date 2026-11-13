#pragma once
#include <cwctype>
namespace cydaw::base {
struct CharacterFunctions {
  static bool isWhitespace(char32_t c) { return std::iswspace(wint_t(c)) != 0; }
  static bool isDigit(char32_t c) { return std::iswdigit(wint_t(c)) != 0; }
  static bool isLetterOrDigit(char32_t c) {
    return std::iswalnum(wint_t(c)) != 0;
  }
  static bool isLowerCase(char32_t c) { return std::iswlower(wint_t(c)) != 0; }
  static bool isUpperCase(char32_t c) { return std::iswupper(wint_t(c)) != 0; }
  static char32_t toLowerCase(char32_t c) {
    return char32_t(std::towlower(wint_t(c)));
  }
  static char32_t toUpperCase(char32_t c) {
    return char32_t(std::towupper(wint_t(c)));
  }
};
} // namespace cydaw::base
