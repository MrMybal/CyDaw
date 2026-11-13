#pragma once
#include <cwctype>
#include <limits>
#include <locale>
namespace cydaw::base {
struct CharacterFunctions {
  static const std::ctype<wchar_t> &facet() {
    static const std::locale locale = [] {
      try {
        return std::locale("");
      } catch (...) {
        return std::locale::classic();
      }
    }();
    return std::use_facet<std::ctype<wchar_t>>(locale);
  }
  static bool category(char32_t c, std::ctype_base::mask mask) {
    return c <= std::numeric_limits<wchar_t>::max() &&
           facet().is(mask, wchar_t(c));
  }
  static bool isDigit(char32_t c) { return c >= '0' && c <= '9'; }
  static bool isLetter(char32_t c) {
    return category(c, std::ctype_base::alpha);
  }
  static bool isLetterOrDigit(char32_t c) { return isDigit(c) || isLetter(c); }
  static bool isLowerCase(char32_t c) {
    return category(c, std::ctype_base::lower);
  }
  static bool isUpperCase(char32_t c) {
    return category(c, std::ctype_base::upper);
  }
  static bool isWhitespace(char32_t c) {
    return category(c, std::ctype_base::space);
  }
  static char32_t toLowerCase(char32_t c) {
    return c <= std::numeric_limits<wchar_t>::max()
               ? char32_t(facet().tolower(wchar_t(c)))
               : c;
  }
  static char32_t toUpperCase(char32_t c) {
    return c <= std::numeric_limits<wchar_t>::max()
               ? char32_t(facet().toupper(wchar_t(c)))
               : c;
  }
};
} // namespace cydaw::base
