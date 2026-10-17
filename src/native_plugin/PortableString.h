#pragma once
// Small UTF-8 value used by the shared drawing code. Implemented with the C++
// standard library; no JUCE headers, binaries, or implementation are used.
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
namespace cydaw::portable
{
using int64 = std::int64_t;
template <class... T> void ignoreUnused(const T &...)
{
}
inline int roundToInt(double v)
{
    return static_cast<int>(std::lround(v));
}
class String
{
    std::string text_;

  public:
    String() = default;
    String(const char *s) : text_(s ? s : "")
    {
    }
    String(std::string s) : text_(std::move(s))
    {
    }
    template <class T>
        requires std::is_arithmetic_v<T>
    String(T v) : text_(std::to_string(v))
    {
    }
    String(double v, int decimals)
    {
        char b[128];
        std::snprintf(b, sizeof(b), "%.*f", std::clamp(decimals, 0, 12), v);
        text_ = b;
    }
    static String fromUTF8(const char *s)
    {
        return s;
    }
    const char *toRawUTF8() const
    {
        return text_.c_str();
    }
    std::string toStdString() const
    {
        return text_;
    }
    bool isEmpty() const
    {
        return text_.empty();
    }
    bool isNotEmpty() const
    {
        return !isEmpty();
    }
    int length() const
    {
        return static_cast<int>(text_.size());
    }
    bool startsWith(const String &s) const
    {
        return text_.starts_with(s.text_);
    }
    bool endsWith(const String &s) const
    {
        return text_.ends_with(s.text_);
    }
    bool contains(const String &s) const
    {
        return text_.find(s.text_) != std::string::npos;
    }
    String substring(int first) const
    {
        return text_.substr(std::min(text_.size(), static_cast<std::size_t>(std::max(0, first))));
    }
    String substring(int first, int last) const
    {
        first = std::clamp(first, 0, length());
        last = std::clamp(last, first, length());
        return text_.substr(first, last - first);
    }
    int lastIndexOf(const String &s) const
    {
        auto p = text_.rfind(s.text_);
        return p == std::string::npos ? -1 : static_cast<int>(p);
    }
    int lastIndexOfChar(char c) const
    {
        auto p = text_.rfind(c);
        return p == std::string::npos ? -1 : static_cast<int>(p);
    }
    String fromLastOccurrenceOf(const String &s, bool include, bool) const
    {
        auto p = lastIndexOf(s);
        return p < 0 ? *this : substring(p + (include ? 0 : s.length()));
    }
    String upToLastOccurrenceOf(const String &s, bool include, bool) const
    {
        auto p = lastIndexOf(s);
        return p < 0 ? *this : substring(0, p + (include ? s.length() : 0));
    }
    String dropLastCharacters(int n) const
    {
        return substring(0, std::max(0, length() - n));
    }
    String trimCharactersAtStart(const String &chars) const
    {
        auto p = text_.find_first_not_of(chars.text_);
        return p == std::string::npos ? String() : String(text_.substr(p));
    }
    char operator[](int i) const
    {
        return i >= 0 && i < length() ? text_[i] : 0;
    }
    String replaceCharacter(char a, char b) const
    {
        auto s = text_;
        std::replace(s.begin(), s.end(), a, b);
        return s;
    }
    String toUpperCase() const
    {
        auto s = text_;
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return s;
    }
    String &operator+=(const String &b)
    {
        text_ += b.text_;
        return *this;
    }
    String &operator<<(const String &b)
    {
        return *this += b;
    }
    friend String operator+(String a, const String &b)
    {
        a += b;
        return a;
    }
    friend bool operator==(const String &, const String &) = default;
    friend auto operator<=>(const String &, const String &) = default;
};
struct StringArray : std::vector<String>
{
    using std::vector<String>::vector;
    void add(String s)
    {
        push_back(std::move(s));
    }
    String joinIntoString(const String &separator) const
    {
        String result;
        for (const auto &s : *this)
        {
            if (result.isNotEmpty())
                result += separator;
            result += s;
        }
        return result;
    }
    bool contains(const String &s) const
    {
        return std::find(begin(), end(), s) != end();
    }
};
} // namespace cydaw::portable
