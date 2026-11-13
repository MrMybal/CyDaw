#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
namespace cydaw::base {
using int64 = std::int64_t;
using uint64 = std::uint64_t;
using uint32 = std::uint32_t;
using uint8 = std::uint8_t;
using UnicodeChar = char32_t;
class Text {
  std::string data_;
  static std::string encode(char32_t c) {
    if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
      c = 0xfffd;
    if (c < 128)
      return std::string(1, char(c));
    std::string s;
    if (c < 0x800) {
      s += char(0xc0 | (c >> 6));
      s += char(0x80 | (c & 63));
    } else if (c < 0x10000) {
      s += char(0xe0 | (c >> 12));
      s += char(0x80 | ((c >> 6) & 63));
      s += char(0x80 | (c & 63));
    } else {
      s += char(0xf0 | (c >> 18));
      s += char(0x80 | ((c >> 12) & 63));
      s += char(0x80 | ((c >> 6) & 63));
      s += char(0x80 | (c & 63));
    }
    return s;
  }
  std::size_t byteAt(int index) const noexcept {
    if (index <= 0)
      return 0;
    std::size_t i = 0;
    while (i < data_.size() && index--) {
      ++i;
      while (i < data_.size() && (uint8(data_[i]) & 0xc0) == 0x80)
        ++i;
    }
    return i;
  }

public:
  Text() = default;
  Text(const char *s) : data_(s ? s : "") {}
  Text(const char *s, int bytes)
      : data_(s ? std::string(s, std::size_t(std::max(0, bytes)))
                : std::string{}) {}
  Text(std::string s) : data_(std::move(s)) {}
  Text(std::string_view s) : data_(s) {}
  Text(char c) : data_(1, c) {}
  template <class T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, Text> &&
             requires(const T &t) { t.toStdString(); })
  Text(const T &t) : data_(t.toStdString()) {}
  template <class T>
    requires(!std::is_same_v<T, Text> &&
             requires { T::fromUTF8(static_cast<const char *>(nullptr)); })
  operator T() const {
    return T::fromUTF8(data_.c_str());
  }
  template <class T>
    requires(requires(T t) { t.text.getAddress(); })
  operator T() const {
    return T(data_.c_str());
  }
  Text(const char32_t *s) {
    if (s)
      while (*s)
        data_ += encode(*s++);
  }
  Text(const wchar_t *s) {
    if (s)
      for (std::size_t i = 0; s[i]; ++i) {
        char32_t c = s[i];
        if constexpr (sizeof(wchar_t) == 2) {
          if (c >= 0xd800 && c <= 0xdbff && s[i + 1] >= 0xdc00 &&
              s[i + 1] <= 0xdfff)
            c = 0x10000 + ((c - 0xd800) << 10) + (s[++i] - 0xdc00);
        }
        data_ += encode(c);
      }
  }
  template <class T>
    requires(std::is_integral_v<T> && !std::is_same_v<T, char>)
  Text(T v) : data_(std::to_string(v)) {}
  Text(double v) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(15) << v;
    data_ = o.str();
  }
  Text(double v, int digits) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    if (digits > 0)
      o << std::fixed << std::setprecision(std::clamp(digits, 0, 20));
    else
      o << std::setprecision(6);
    o << v;
    data_ = o.str();
  }
  Text(float v) : Text(double(v)) {}
  Text(float v, int digits) : Text(double(v), digits) {}
  static Text fromUTF8(const char *s, int bytes = -1) {
    return s ? Text(bytes < 0 ? std::string(s)
                              : std::string(s, std::size_t(bytes)))
             : Text{};
  }
  static Text charToString(char32_t c) { return encode(c); }
  static Text repeatedString(const Text &t, int count) {
    Text r;
    for (int i = 0; i < count; ++i)
      r += t;
    return r;
  }
  template <class T> static Text toHexString(T value) {
    std::ostringstream o;
    o << std::hex << value;
    return o.str();
  }
  static Text toHexString(const void *bytes, int size, int group = 0) {
    const auto *p = static_cast<const uint8 *>(bytes);
    Text r;
    const char *hex = "0123456789abcdef";
    for (int i = 0; i < size; ++i) {
      if (group > 0 && i && i % group == 0)
        r += ' ';
      r.data_ += hex[p[i] >> 4];
      r.data_ += hex[p[i] & 15];
    }
    return r;
  }
  const std::string &toStdString() const noexcept { return data_; }
  const char *toRawUTF8() const noexcept { return data_.c_str(); }
  std::size_t getNumBytesAsUTF8() const noexcept { return data_.size(); }
  struct Utf8 {
    const char *p;
    const char *getAddress() const noexcept { return p; }
    operator const char *() const noexcept { return p; }
    std::size_t sizeInBytes() const noexcept {
      return std::char_traits<char>::length(p) + 1;
    }
  };
  struct Cursor {
    const Text *owner;
    int index = 0;
    bool isEmpty() const { return index >= owner->length(); }
    char32_t operator*() const { return (*owner)[index]; }
    Cursor &operator++() {
      ++index;
      return *this;
    }
    Cursor operator++(int) {
      auto prior = *this;
      ++index;
      return prior;
    }
    char32_t getAndAdvance() { return (*owner)[index++]; }
  };
  Cursor getCharPointer() const { return {this, 0}; }
  Utf8 toUTF8() const noexcept { return {data_.c_str()}; }
  struct Wide {
    std::wstring value;
    const wchar_t *getAddress() const noexcept { return value.c_str(); }
    operator const wchar_t *() const noexcept { return value.c_str(); }
  };
  Wide toWideCharPointer() const { return {toWideString()}; }
  int copyToUTF8(char *destination, int maximumBytes) const {
    if (maximumBytes <= 0)
      return int(data_.size() + 1);
    auto n = std::min<std::size_t>(data_.size(), std::size_t(maximumBytes - 1));
    if (destination) {
      std::copy_n(data_.data(), n, destination);
      destination[n] = 0;
    }
    return int(n + 1);
  }
  template <class... A> static Text formatted(const char *format, A... args) {
    int n = std::snprintf(nullptr, 0, format, args...);
    if (n < 0 || n > 1024 * 1024)
      return {};
    std::string out(std::size_t(n) + 1, '\0');
    std::snprintf(out.data(), out.size(), format, args...);
    out.resize(n);
    return out;
  }
  static Text fromUTF16(const char16_t* data, std::size_t size) {
    Text out; for(std::size_t i=0;i<size&&data[i];++i){char32_t c=data[i];if(c>=0xd800&&c<=0xdbff){if(i+1<size&&data[i+1]>=0xdc00&&data[i+1]<=0xdfff)c=0x10000+((c-0xd800)<<10)+(data[++i]-0xdc00);else c=0xfffd;}else if(c>=0xdc00&&c<=0xdfff)c=0xfffd;out+=charToString(c);}return out;
  }
  static bool isValidUTF8(const char* data,std::size_t size) {
    for(std::size_t i=0;i<size;){auto a=uint8(data[i++]);if(a<128)continue;int n=a>=0xc2&&a<=0xdf?1:a>=0xe0&&a<=0xef?2:a>=0xf0&&a<=0xf4?3:-1;if(n<0||i+std::size_t(n)>size)return false;char32_t c=a&((1<<(6-n))-1);for(int k=0;k<n;++k){auto b=uint8(data[i++]);if((b&0xc0)!=0x80)return false;c=(c<<6)|(b&63);}if((n==1&&c<0x80)||(n==2&&c<0x800)||(n==3&&c<0x10000)||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;}return true;
  }
  std::wstring toWideString() const {
    std::wstring out;
    for (int i = 0; i < length(); ++i) {
      auto c = (*this)[i];
      if constexpr (sizeof(wchar_t) == 2) {
        if (c > 0xffff) {
          c -= 0x10000;
          out += wchar_t(0xd800 + (c >> 10));
          out += wchar_t(0xdc00 + (c & 1023));
          continue;
        }
      }
      out += wchar_t(c);
    }
    return out;
  }
  int length() const noexcept {
    int n = 0;
    for (auto c : data_)
      if ((uint8(c) & 0xc0) != 0x80)
        ++n;
    return n;
  }
  bool isEmpty() const noexcept { return data_.empty(); }
  bool isNotEmpty() const noexcept { return !data_.empty(); }
  void clear() noexcept { data_.clear(); }
  void preallocateBytes(std::size_t n) { data_.reserve(n); }
  char32_t operator[](int index) const noexcept {
    auto p = byteAt(index);
    if (p == data_.size())
      return 0;
    uint8 c = data_[p++];
    if (c < 128)
      return c;
    unsigned n = c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
    char32_t r = c & ((1u << (6 - n)) - 1);
    while (n-- && p < data_.size())
      r = (r << 6) | (uint8(data_[p++]) & 63);
    return r;
  }
  char32_t getLastCharacter() const noexcept {
    return isEmpty() ? 0 : (*this)[length() - 1];
  }
  Text substring(int start, int end = std::numeric_limits<int>::max()) const {
    auto a = byteAt(start);
    auto b = byteAt(std::max(start, end));
    return data_.substr(a, b - a);
  }
  Text dropLastCharacters(int n) const {
    return substring(0, std::max(0, length() - n));
  }
  Text getLastCharacters(int n) const {
    return substring(std::max(0, length() - n));
  }
  Text trim() const { return trimStart().trimEnd(); }
  Text trimStart() const {
    int i = 0;
    while (i < length() && std::iswspace(wint_t((*this)[i])))
      ++i;
    return substring(i);
  }
  Text trimEnd() const {
    int i = length();
    while (i && std::iswspace(wint_t((*this)[i - 1])))
      --i;
    return substring(0, i);
  }
  Text toLowerCase() const {
    Text out;
    for (int i = 0; i < length(); ++i)
      out.data_ += encode(char32_t(std::towlower(wint_t((*this)[i]))));
    return out;
  }
  Text toUpperCase() const {
    Text out;
    for (int i = 0; i < length(); ++i)
      out.data_ += encode(char32_t(std::towupper(wint_t((*this)[i]))));
    return out;
  }
  bool startsWith(const Text &t) const noexcept {
    return data_.starts_with(t.data_);
  }
  bool endsWith(const Text &t) const noexcept {
    return data_.ends_with(t.data_);
  }
  bool startsWithIgnoreCase(const Text &t) const {
    return toLowerCase().startsWith(t.toLowerCase());
  }
  bool endsWithIgnoreCase(const Text &t) const {
    return toLowerCase().endsWith(t.toLowerCase());
  }
  bool startsWithChar(char32_t c) const noexcept { return (*this)[0] == c; }
  bool endsWithChar(char32_t c) const noexcept {
    return !isEmpty() && getLastCharacter() == c;
  }
  bool equalsIgnoreCase(const Text &t) const {
    return toLowerCase() == t.toLowerCase();
  }
  template <class T>
    requires(requires(const T &t) { t.toStdString(); })
  bool equalsIgnoreCase(const T &t) const {
    return equalsIgnoreCase(Text(t.toStdString()));
  }
  bool contains(const Text &t) const noexcept {
    return data_.find(t.data_) != std::string::npos;
  }
  bool containsIgnoreCase(const Text &t) const {
    return toLowerCase().contains(t.toLowerCase());
  }
  bool containsChar(char32_t c) const { return contains(charToString(c)); }
  bool containsAnyOf(const Text &chars) const {
    for (int i = 0; i < chars.length(); ++i)
      if (containsChar(chars[i]))
        return true;
    return false;
  }
  bool containsOnly(const Text &chars) const {
    for (int i = 0; i < length(); ++i)
      if (!chars.containsChar((*this)[i]))
        return false;
    return true;
  }
  int indexOf(const Text &t) const noexcept {
    auto p = data_.find(t.data_);
    if (p == std::string::npos)
      return -1;
    int n = 0;
    for (std::size_t i = 0; i < p; ++i)
      if ((uint8(data_[i]) & 0xc0) != 0x80)
        ++n;
    return n;
  }
  int indexOf(int start, const Text &t) const {
    auto v = substring(start).indexOf(t);
    return v < 0 ? -1 : start + v;
  }
  int indexOfIgnoreCase(const Text &t) const {
    return toLowerCase().indexOf(t.toLowerCase());
  }
  int indexOfChar(char32_t c) const { return indexOf(charToString(c)); }
  int indexOfChar(int start, char32_t c) const {
    return indexOf(start, charToString(c));
  }
  int lastIndexOf(const Text &t) const {
    auto p = data_.rfind(t.data_);
    return p == std::string::npos ? -1 : Text(data_.substr(0, p)).length();
  }
  int lastIndexOfChar(char32_t c) const { return lastIndexOf(charToString(c)); }
  Text fromFirstOccurrenceOf(const Text &t, bool include,
                             bool ignoreCase) const {
    int i = ignoreCase ? indexOfIgnoreCase(t) : indexOf(t);
    return i < 0 ? Text{} : substring(i + (include ? 0 : t.length()));
  }
  Text upToFirstOccurrenceOf(const Text &t, bool include,
                             bool ignoreCase) const {
    int i = ignoreCase ? indexOfIgnoreCase(t) : indexOf(t);
    return i < 0 ? *this : substring(0, i + (include ? t.length() : 0));
  }
  Text fromLastOccurrenceOf(const Text &t, bool include,
                            bool ignoreCase) const {
    auto source = ignoreCase ? toLowerCase() : *this;
    int i = source.lastIndexOf(ignoreCase ? t.toLowerCase() : t);
    return i < 0 ? Text{} : substring(i + (include ? 0 : t.length()));
  }
  Text upToLastOccurrenceOf(const Text &t, bool include,
                            bool ignoreCase) const {
    auto source = ignoreCase ? toLowerCase() : *this;
    int i = source.lastIndexOf(ignoreCase ? t.toLowerCase() : t);
    return i < 0 ? *this : substring(0, i + (include ? t.length() : 0));
  }
  Text replace(const Text &from, const Text &to,
               bool ignoreCase = false) const {
    if (from.isEmpty())
      return *this;
    if (!ignoreCase) {
      std::string out;
      out.reserve(data_.size());
      std::size_t position=0;
      for (;;) {
        const auto hit=data_.find(from.data_,position);
        if(hit==std::string::npos){out.append(data_,position,std::string::npos);break;}
        out.append(data_,position,hit-position);out+=to.data_;position=hit+from.data_.size();
      }
      return out;
    }
    Text out;
    int pos = 0;
    for (;;) {
      auto rest = substring(pos);
      int hit = ignoreCase ? rest.indexOfIgnoreCase(from) : rest.indexOf(from);
      if (hit < 0) {
        out += rest;
        break;
      }
      out += rest.substring(0, hit);
      out += to;
      pos += hit + from.length();
    }
    return out;
  }
  Text replaceCharacter(char32_t from, char32_t to) const {
    return replace(charToString(from), charToString(to));
  }
  Text replaceCharacters(const Text &from, const Text &to) const {
    Text out;
    for (int i = 0; i < length(); ++i) {
      auto c = (*this)[i];
      auto at = from.indexOfChar(c);
      out += charToString(at >= 0 && at < to.length() ? to[at] : c);
    }
    return out;
  }
  Text retainCharacters(const Text &chars) const {
    Text out;
    for (int i = 0; i < length(); ++i)
      if (chars.containsChar((*this)[i]))
        out += charToString((*this)[i]);
    return out;
  }
  Text removeCharacters(const Text &chars) const {
    Text out;
    for (int i = 0; i < length(); ++i)
      if (!chars.containsChar((*this)[i]))
        out += charToString((*this)[i]);
    return out;
  }
  Text trimCharactersAtStart(const Text &chars) const {
    int n = 0;
    while (n < length() && chars.containsChar((*this)[n]))
      ++n;
    return substring(n);
  }
  Text trimCharactersAtEnd(const Text &chars) const {
    int n = length();
    while (n && chars.containsChar((*this)[n - 1]))
      --n;
    return substring(0, n);
  }
  Text initialSectionContainingOnly(const Text &chars) const {
    int n = 0;
    while (n < length() && chars.containsChar((*this)[n]))
      ++n;
    return substring(0, n);
  }
  Text initialSectionNotContaining(const Text &chars) const {
    int n = 0;
    while (n < length() && !chars.containsChar((*this)[n]))
      ++n;
    return substring(0, n);
  }
  int indexOfAnyOf(const Text &chars, int start = 0,
                   bool ignoreCase = false) const {
    auto source = ignoreCase ? toLowerCase() : *this;
    auto set = ignoreCase ? chars.toLowerCase() : chars;
    for (int i = std::max(0, start); i < source.length(); ++i)
      if (set.containsChar(source[i]))
        return i;
    return -1;
  }
  int lastIndexOfAnyOf(const Text &chars, bool ignoreCase = false) const {
    auto source = ignoreCase ? toLowerCase() : *this;
    auto set = ignoreCase ? chars.toLowerCase() : chars;
    for (int i = source.length() - 1; i >= 0; --i)
      if (set.containsChar(source[i]))
        return i;
    return -1;
  }
  Text replaceSection(int start, int count, const Text &replacement) const {
    return substring(0, start) + replacement +
           substring(start + std::max(0, count));
  }
  int getTrailingIntValue() const {
    int start = length();
    while (start && (*this)[start - 1] >= '0' && (*this)[start - 1] <= '9')
      --start;
    if (start && (*this)[start - 1] == '-')
      --start;
    return substring(start).getIntValue();
  }
  Text paddedLeft(char c, int count) const {
    return Text(std::string(std::size_t(std::max(0, count - length())), c)) +
           *this;
  }
  Text paddedRight(char c, int count) const {
    return *this +
           Text(std::string(std::size_t(std::max(0, count - length())), c));
  }
  Text quoted(char c = '"') const { return Text(c) + *this + Text(c); }
  Text unquoted() const {
    return length() >= 2 && ((*this)[0] == '"' || (*this)[0] == '\'') &&
                   getLastCharacter() == (*this)[0]
               ? substring(1, length() - 1)
               : *this;
  }
  double getDoubleValue() const noexcept {
    char *end = nullptr;
    return std::strtod(data_.c_str(), &end);
  }
  float getFloatValue() const noexcept { return float(getDoubleValue()); }
  int getIntValue() const noexcept { return int(getLargeIntValue()); }
  int64 getLargeIntValue() const noexcept {
    int64 v = 0;
    auto s = trimStart();
    auto *p = s.data_.data();
    if (*p == '+')
      ++p;
    (void)std::from_chars(p, s.data_.data() + s.data_.size(), v);
    return v;
  }
  int64 getHexValue64() const noexcept {
    uint64 v = 0;
    auto s = trim().data_;
    if (s.starts_with("0x") || s.starts_with("0X"))
      s.erase(0, 2);
    (void)std::from_chars(s.data(), s.data() + s.size(), v, 16);
    return int64(v);
  }
  int getHexValue32() const noexcept { return int(getHexValue64()); }
  int compare(const Text &t) const noexcept { return data_.compare(t.data_); }
  int compareIgnoreCase(const Text &t) const {
    return toLowerCase().compare(t.toLowerCase());
  }
  int compareNatural(const Text &other, bool caseSensitive = false) const {
    const auto a = caseSensitive ? *this : toLowerCase(),
               b = caseSensitive ? other : other.toLowerCase();
    int i = 0, j = 0;
    while (i < a.length() && j < b.length()) {
      auto x = a[i], y = b[j];
      if (x >= '0' && x <= '9' && y >= '0' && y <= '9') {
        int ae = i, be = j;
        while (ae < a.length() && a[ae] >= '0' && a[ae] <= '9')
          ++ae;
        while (be < b.length() && b[be] >= '0' && b[be] <= '9')
          ++be;
        int az = i, bz = j;
        while (az < ae && a[az] == '0')
          ++az;
        while (bz < be && b[bz] == '0')
          ++bz;
        if (ae - az != be - bz)
          return ae - az < be - bz ? -1 : 1;
        auto order = a.substring(az, ae).compare(b.substring(bz, be));
        if (order)
          return order;
        i = ae;
        j = be;
        continue;
      }
      if (x != y)
        return x < y ? -1 : 1;
      ++i;
      ++j;
    }
    return (i < a.length()) - (j < b.length());
  }
  int hashCode() const noexcept {
    uint32 h = 0;
    for (int i = 0; i < length(); ++i)
      h = h * 31 + uint32((*this)[i]);
    return int(h);
  }
  int64 hashCode64() const noexcept {
    uint64 h = 0;
    for (int i = 0; i < length(); ++i)
      h = h * 101 + uint64((*this)[i]);
    return int64(h);
  }
  template<class T>requires(std::is_arithmetic_v<T> && !std::is_same_v<T,char> && !std::is_same_v<T,wchar_t> && !std::is_same_v<T,char32_t>)
  Text& operator+=(T value){return *this+=Text(value);}
  Text &operator+=(char32_t c) {
    data_ += encode(c);
    return *this;
  }
  Text &operator+=(const Text &t) {
    data_ += t.data_;
    return *this;
  }
  template <class T> Text &operator<<(const T &t) { return *this += Text(t); }
  friend Text operator+(Text a, const Text &b) { return a += b; }
  friend bool operator==(const Text &, const Text &) = default;
  friend auto operator<=>(const Text &, const Text &) = default;
  template <class T>
    requires(requires(const T &t) { t.toStdString(); })
  friend bool operator==(const Text &a, const T &b) {
    return a.data_ == b.toStdString();
  }
  template <class T>
    requires(requires(const T &t) { t.toStdString(); })
  friend Text operator+(Text a, const T &b) {
    return a += Text(b.toStdString());
  }
  template <class T>
    requires(requires(const T &t) { t.toStdString(); })
  friend Text operator+(const T &a, const Text &b) {
    return Text(a.toStdString()) + b;
  }
  template <class T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, Text> &&
             requires(const T &t) { t.toStdString(); })
  Text &operator=(const T &t) {
    data_ = t.toStdString();
    return *this;
  }
  template <class T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, Text> &&
             requires(const T &t) { t.toStdString(); })
  friend T &operator<<(T &out, const Text &text) {
    out << text.toRawUTF8();
    return out;
  }
  friend std::ostream &operator<<(std::ostream &out, const Text &text) {
    return out << text.data_;
  }
  auto begin() const { return data_.begin(); }
  auto end() const { return data_.end(); }
};
using StringRef = Text;
class TextList : public std::vector<Text> {
public:
  using std::vector<Text>::vector;
  int size() const noexcept { return int(std::vector<Text>::size()); }
  bool isEmpty() const noexcept { return empty(); }
  void add(Text s) { push_back(std::move(s)); }
  bool addIfNotAlreadyThere(Text s, bool ignoreCase = false) {
    if (contains(s, ignoreCase))
      return false;
    add(std::move(s));
    return true;
  }
  template <class T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, TextList> &&
             requires(const T &a) {
               a.joinIntoString("");
               a.begin();
               a.end();
             })
  TextList(const T &other) {
    for (auto &v : other)
      add(Text(v.toStdString()));
  }
  TextList() = default;
  explicit TextList(Text text) { add(std::move(text)); }
  TextList(const char *const *strings, int count = -1) {
    if (strings)
      for (int i = 0; (count < 0 ? strings[i] != nullptr : i < count); ++i)
        add(Text(strings[i]));
  }
  template <class T>
    requires(!std::is_same_v<T, TextList> &&
             requires { T::fromTokens("", false); })
  operator T() const {
    T out;
    for (auto &text : *this)
      out.add(text.toRawUTF8());
    return out;
  }
  const Text &getReference(int i) const { return (*this)[i]; }
  Text &getReference(int i) { return (*this)[i]; }
  void removeString(const Text &text, bool ignoreCase = false) {
    std::erase_if(*this, [&](const Text &s) {
      return ignoreCase ? s.equalsIgnoreCase(text) : s == text;
    });
  }
  int addTokens(const Text &text, const Text &breaks, const Text &quotes) {
    auto tokens = fromTokens(text, breaks, quotes);
    int n = tokens.size();
    addArray(tokens);
    return n;
  }
  void insert(int index, Text text) {
    std::vector<Text>::insert(begin() + std::clamp(index, 0, size()),
                              std::move(text));
  }
  void clearQuick() { clear(); }
  void addArray(const TextList &a) {
    std::vector<Text>::insert(end(), a.begin(), a.end());
  }
  bool contains(const Text &s, bool ic = false) const {
    return indexOf(s, ic) >= 0;
  }
  int indexOf(const Text &s, bool ic = false) const {
    for (int i = 0; i < size(); ++i)
      if (ic ? (*this)[i].equalsIgnoreCase(s) : (*this)[i] == s)
        return i;
    return -1;
  }
  void removeRange(int first, int count) {
    first = std::clamp(first, 0, size());
    erase(begin() + first,
          begin() + std::min(size(), first + std::max(0, count)));
  }
  void remove(int i) {
    if (i >= 0 && i < size())
      erase(begin() + i);
  }
  void set(int i, Text s) {
    if (i >= 0) {
      if (i >= size())
        resize(i + 1);
      (*this)[i] = std::move(s);
    }
  }
  void removeEmptyStrings(bool trim = false) {
    std::erase_if(*this, [&](const Text &t) {
      return trim ? t.trim().isEmpty() : t.isEmpty();
    });
  }
  void trim() {
    for (auto &s : *this)
      s = s.trim();
  }
  void removeDuplicates(bool ic) {
    TextList out;
    for (auto &s : *this)
      out.addIfNotAlreadyThere(s, ic);
    swap(out);
  }
  void sort(bool ic) {
    std::sort(begin(), end(), [&](const Text &a, const Text &b) {
      return ic ? a.compareIgnoreCase(b) < 0 : a < b;
    });
  }
  void sortNatural() {
    std::stable_sort(begin(), end(), [](const Text &a, const Text &b) {
      return a.compareNatural(b) < 0;
    });
  }
  Text joinIntoString(const Text &separator, int start = 0,
                      int count = -1) const {
    Text r;
    int last = count < 0 ? size() : std::min(size(), start + count);
    for (int i = std::max(0, start); i < last; ++i) {
      if (i > start)
        r += separator;
      r += (*this)[i];
    }
    return r;
  }
  static TextList fromTokens(const Text &s, const Text &separators,
                             const Text &quotes) {
    TextList list;
    Text token;
    char32_t quoted = 0;
    for (int i = 0; i < s.length(); ++i) {
      auto c = s[i];
      if (quotes.containsChar(c)) {
        if (!quoted)
          quoted = c;
        else if (quoted == c)
          quoted = 0;
        token += Text::charToString(c);
      } else if (!quoted && separators.containsChar(c)) {
        list.add(token);
        token.clear();
      } else
        token += Text::charToString(c);
    }
    list.add(token);
    return list;
  }
  static TextList fromTokens(const Text &s, bool quotes) {
    return fromTokens(s, " \t\r\n", quotes ? "\"'" : "");
  }
  static TextList fromLines(const Text &s) {
    TextList a;
    int begin = 0;
    for (int i = 0; i < s.length(); ++i)
      if (s[i] == '\r' || s[i] == '\n') {
        a.add(s.substring(begin, i));
        if (s[i] == '\r' && s[i + 1] == '\n')
          ++i;
        begin = i + 1;
      }
    a.add(s.substring(begin));
    return a;
  }
  void addLines(const Text &s) { addArray(fromLines(s)); }
  void ensureStorageAllocated(int count) {
    reserve(std::size_t(std::max(0, count)));
  }
};
class Identifier {
  Text value_;

public:
  Identifier() = default;
  Identifier(const char *s) : value_(s) {}
  Identifier(Text s) : value_(std::move(s)) {}
  template <class T>
    requires(requires(const T &t) { t.toString().toStdString(); })
  Identifier(const T &t) : value_(t.toString().toStdString()) {}
  template <class T>
    requires(requires(const T &t) { t.toStdString(); })
  Identifier(const T &t) : value_(t.toStdString()) {}
  template <class T>
    requires(!std::is_same_v<T, Identifier> &&
             requires(const T &t) { t.toString().toStdString(); } &&
             std::is_constructible_v<T, const char *>)
  operator T() const {
    return T(value_.toRawUTF8());
  }
  const Text &toString() const noexcept { return value_; }
  bool isValid() const noexcept { return value_.isNotEmpty(); }
  friend bool operator==(const Identifier &, const Identifier &) = default;
  friend auto operator<=>(const Identifier &, const Identifier &) = default;
};
class Result {
  Text message_;

public:
  Result() = default;
  template <class T>
    requires(requires(const T &t) {
      t.wasOk();
      t.getErrorMessage().toStdString();
    })
  Result(const T &r)
      : message_(r.wasOk() ? Text{} : Text(r.getErrorMessage().toStdString())) {
  }
  template <class T>
    requires(!std::is_same_v<T, Result> &&
             requires {
               T::ok();
               T::fail(static_cast<const char *>(nullptr));
             })
  operator T() const {
    return wasOk() ? T::ok() : T::fail(message_.toRawUTF8());
  }
  template <class T>
    requires(requires(const T &t) { t.toStdString(); })
  static Result fail(const T &t) {
    return fail(Text(t.toStdString()));
  }
  static Result ok() { return {}; }
  static Result fail(Text m) {
    Result r;
    r.message_ = m.isEmpty() ? Text("Operation failed") : std::move(m);
    return r;
  }
  bool wasOk() const noexcept { return message_.isEmpty(); }
  bool failed() const noexcept { return !wasOk(); }
  const Text &getErrorMessage() const noexcept { return message_; }
  explicit operator bool() const noexcept { return wasOk(); }
};
template <class T> constexpr T jlimit(T lo, T hi, T x) {
  return std::clamp(x, lo, hi);
}
template <class... T> constexpr auto jmin(T... a) { return std::min({a...}); }
template <class... T> constexpr auto jmax(T... a) { return std::max({a...}); }
template <class T> int roundToInt(T v) { return int(std::lround(v)); }
template <class... T> void ignoreUnused(const T &...) {};
} // namespace cydaw::base
namespace std {
template <> struct hash<cydaw::base::Text> {
  std::size_t operator()(const cydaw::base::Text &s) const noexcept {
    return hash<string>{}(s.toStdString());
  }
};
template <> struct hash<cydaw::base::Identifier> {
  std::size_t operator()(const cydaw::base::Identifier &s) const noexcept {
    return hash<cydaw::base::Text>{}(s.toString());
  }
};
} // namespace std
