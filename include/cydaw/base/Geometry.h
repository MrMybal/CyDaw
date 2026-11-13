#pragma once
#include "Text.h"
#include <numbers>
namespace cydaw::base {
template <class T> struct Point {
  T x{}, y{};
  constexpr Point() = default;
  constexpr Point(T a, T b) : x(a), y(b) {}
  template <class U>
  explicit Point(Point<U> other) : x(T(other.x)), y(T(other.y)) {}
  template <class Foreign>
    requires(!std::is_same_v<std::remove_cvref_t<Foreign>, Point> &&
             requires(const Foreign &p) {
               p.getX();
               p.getY();
             })
  Point(const Foreign &p) : x(T(p.getX())), y(T(p.getY())) {}
  template <class Foreign>
    requires(!std::is_same_v<Foreign, Point> &&
             requires(const Foreign &p) {
               p.getX();
               p.getY();
             } && std::is_constructible_v<Foreign, T, T>)
  operator Foreign() const {
    return Foreign(x, y);
  }
  constexpr T getX() const { return x; }
  constexpr T getY() const { return y; }
  constexpr Point withX(T a) const { return {a, y}; }
  constexpr Point withY(T a) const { return {x, a}; }
  constexpr Point translated(T dx, T dy) const {
    return {T(x + dx), T(y + dy)};
  }
  Point<float> toFloat() const { return {float(x), float(y)}; }
  Point<int> toInt() const { return {int(x), int(y)}; }
  T getDistanceFrom(Point other) const {
    return T(std::hypot(double(x - other.x), double(y - other.y)));
  }
  T getDistanceSquaredFrom(Point other) const {
    return T((x - other.x) * (x - other.x) + (y - other.y) * (y - other.y));
  }
  T getDistanceFromOrigin() const {
    return T(std::hypot(double(x), double(y)));
  }
  bool isFinite() const {
    return std::isfinite(double(x)) && std::isfinite(double(y));
  }
  Point &operator+=(Point p) {
    x += p.x;
    y += p.y;
    return *this;
  }
  Point &operator-=(Point p) {
    x -= p.x;
    y -= p.y;
    return *this;
  }
  template <class U> Point &operator*=(U factor) {
    x = T(x * factor);
    y = T(y * factor);
    return *this;
  }
  template <class U> Point &operator/=(U divisor) {
    x = T(x / divisor);
    y = T(y / divisor);
    return *this;
  }
  friend Point operator+(Point a, Point b) { return a += b; }
  friend Point operator-(Point a, Point b) { return a -= b; }
  Point operator-() const { return {-x, -y}; }
  template <class U> friend Point operator*(Point a, U b) { return a *= b; }
  template <class U> friend Point operator*(U b, Point a) { return a *= b; }
  template <class U> friend Point operator/(Point a, U b) { return a /= b; }
  friend bool operator==(Point, Point) = default;
};
template <class T> class Rectangle {
  T x_{}, y_{}, w_{}, h_{};

public:
  constexpr Rectangle() = default;
  constexpr Rectangle(T x, T y, T w, T h) : x_(x), y_(y), w_(w), h_(h) {}
  constexpr Rectangle(T w, T h) : w_(w), h_(h) {}
  Rectangle(Point<T> a, Point<T> b)
      : Rectangle(std::min(a.x, b.x), std::min(a.y, b.y), std::abs(a.x - b.x),
                  std::abs(a.y - b.y)) {}
  T getX() const { return x_; }
  T getY() const { return y_; }
  T getWidth() const { return w_; }
  T getHeight() const { return h_; }
  T getRight() const { return x_ + w_; }
  T getBottom() const { return y_ + h_; }
  T getCentreX() const { return x_ + w_ / T(2); }
  T getCentreY() const { return y_ + h_ / T(2); }
  Point<T> getCentre() const { return {getCentreX(), getCentreY()}; }
  Point<T> getPosition() const { return {x_, y_}; }
  Point<T> getTopLeft() const { return {x_, y_}; }
  Point<T> getBottomRight() const { return {getRight(), getBottom()}; }
  bool isEmpty() const { return w_ <= 0 || h_ <= 0; }
  bool contains(Point<T> p) const {
    return p.x >= x_ && p.y >= y_ && p.x < getRight() && p.y < getBottom();
  }
  bool contains(T x, T y) const { return contains({x, y}); }
  bool contains(Rectangle r) const {
    return r.x_ >= x_ && r.y_ >= y_ && r.getRight() <= getRight() &&
           r.getBottom() <= getBottom();
  }
  bool intersects(Rectangle r) const {
    return !isEmpty() && !r.isEmpty() && x_ < r.getRight() &&
           r.x_ < getRight() && y_ < r.getBottom() && r.y_ < getBottom();
  }
  Rectangle getIntersection(Rectangle r) const {
    auto x = std::max(x_, r.x_), y = std::max(y_, r.y_);
    return {x, y, std::max(T{}, std::min(getRight(), r.getRight()) - x),
            std::max(T{}, std::min(getBottom(), r.getBottom()) - y)};
  }
  Rectangle getUnion(Rectangle r) const {
    if (isEmpty())
      return r;
    if (r.isEmpty())
      return *this;
    auto x = std::min(x_, r.x_), y = std::min(y_, r.y_);
    return {x, y, std::max(getRight(), r.getRight()) - x,
            std::max(getBottom(), r.getBottom()) - y};
  }
  Rectangle reduced(T x, T y) const {
    return {x_ + x, y_ + y, std::max(T{}, w_ - x - x),
            std::max(T{}, h_ - y - y)};
  }
  Rectangle reduced(T amount) const { return reduced(amount, amount); }
  Rectangle expanded(T x, T y) const {
    return {x_ - x, y_ - y, w_ + x + x, h_ + y + y};
  }
  Rectangle expanded(T amount) const { return expanded(amount, amount); }
  Rectangle translated(T x, T y) const { return {x_ + x, y_ + y, w_, h_}; }
  Rectangle withX(T x) const { return {x, y_, w_, h_}; }
  Rectangle withY(T y) const { return {x_, y, w_, h_}; }
  Rectangle withWidth(T w) const { return {x_, y_, w, h_}; }
  Rectangle withHeight(T h) const { return {x_, y_, w_, h}; }
  Rectangle withPosition(Point<T> p) const { return {p.x, p.y, w_, h_}; }
  Rectangle withSizeKeepingCentre(T w, T h) const {
    return {getCentreX() - w / T(2), getCentreY() - h / T(2), w, h};
  }
  Rectangle<float> toFloat() const {
    return {float(x_), float(y_), float(w_), float(h_)};
  }
  Rectangle<int> toNearestInt() const {
    return {int(std::lround(x_)), int(std::lround(y_)), int(std::lround(w_)),
            int(std::lround(h_))};
  }
  Rectangle<int> getSmallestIntegerContainer() const {
    int x = int(std::floor(x_)), y = int(std::floor(y_));
    return {x, y, int(std::ceil(getRight())) - x,
            int(std::ceil(getBottom())) - y};
  }
  friend bool operator==(const Rectangle &, const Rectangle &) = default;
};
template <class T> class Range {
  T begin_{}, end_{};

public:
  Range() = default;
  Range(T a, T b) : begin_(a), end_(b) {}
  T getStart() const { return begin_; }
  T getEnd() const { return end_; }
  T getLength() const { return end_ - begin_; }
  bool isEmpty() const { return end_ <= begin_; }
  bool contains(T v) const { return v >= begin_ && v < end_; }
  T clipValue(T v) const { return std::clamp(v, begin_, end_); }
  friend bool operator==(const Range &, const Range &) = default;
};
class Colour {
  std::uint32_t argb_{};
  static std::uint8_t byte(float v) {
    return std::uint8_t(
        std::lround(std::clamp(std::isfinite(v) ? v : 0.f, 0.f, 1.f) * 255.f));
  }

public:
  constexpr Colour() = default;
  constexpr explicit Colour(std::uint32_t argb) : argb_(argb) {}
  constexpr Colour(std::uint8_t r, std::uint8_t g, std::uint8_t b)
      : argb_(0xff000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) |
              b) {}
  template <class Foreign>
    requires(!std::is_same_v<std::remove_cvref_t<Foreign>, Colour> &&
             requires(const Foreign &c) { c.getARGB(); })
  Colour(const Foreign &c) : argb_(c.getARGB()) {}
  template <class Foreign>
    requires(!std::is_same_v<Foreign, Colour> &&
             requires(const Foreign &c) { c.getARGB(); } &&
             std::is_constructible_v<Foreign, std::uint32_t>)
  operator Foreign() const {
    return Foreign(argb_);
  }
  constexpr std::uint32_t getARGB() const { return argb_; }
  constexpr std::uint8_t getRed() const { return std::uint8_t(argb_ >> 16); }
  constexpr std::uint8_t getGreen() const { return std::uint8_t(argb_ >> 8); }
  constexpr std::uint8_t getBlue() const { return std::uint8_t(argb_); }
  constexpr std::uint8_t getAlpha() const { return std::uint8_t(argb_ >> 24); }
  float getFloatAlpha() const { return getAlpha() / 255.f; }
  float getFloatRed() const { return getRed() / 255.f; }
  float getFloatGreen() const { return getGreen() / 255.f; }
  float getFloatBlue() const { return getBlue() / 255.f; }
  float getBrightness() const {
    return std::max({getFloatRed(), getFloatGreen(), getFloatBlue()});
  }
  float getSaturation() const {
    auto hi = getBrightness(),
         lo = std::min({getFloatRed(), getFloatGreen(), getFloatBlue()});
    return hi > 0 ? (hi - lo) / hi : 0;
  }
  static Colour fromRGBA(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                         std::uint8_t a) {
    return Colour((std::uint32_t(a) << 24) | (std::uint32_t(r) << 16) |
                  (std::uint32_t(g) << 8) | b);
  }
  static Colour fromFloatRGBA(float r, float g, float b, float a) {
    return fromRGBA(byte(r), byte(g), byte(b), byte(a));
  }
  static Colour fromHSV(float hue, float saturation, float brightness,
                        float alpha) {
    hue -= std::floor(hue);
    float h = hue * 6;
    int region = int(h);
    float f = h - region, p = brightness * (1 - saturation),
          q = brightness * (1 - f * saturation),
          t = brightness * (1 - (1 - f) * saturation);
    switch (region) {
    case 0:
      return fromFloatRGBA(brightness, t, p, alpha);
    case 1:
      return fromFloatRGBA(q, brightness, p, alpha);
    case 2:
      return fromFloatRGBA(p, brightness, t, alpha);
    case 3:
      return fromFloatRGBA(p, q, brightness, alpha);
    case 4:
      return fromFloatRGBA(t, p, brightness, alpha);
    default:
      return fromFloatRGBA(brightness, p, q, alpha);
    }
  }
  Colour withAlpha(float a) const {
    return fromRGBA(getRed(), getGreen(), getBlue(), byte(a));
  }
  Colour withMultipliedAlpha(float a) const {
    return withAlpha(getFloatAlpha() * a);
  }
  Colour withBrightness(float brightness) const {
    auto hi = getBrightness();
    if (hi <= 0)
      return fromFloatRGBA(brightness, brightness, brightness, getFloatAlpha());
    auto scale = brightness / hi;
    return fromFloatRGBA(getFloatRed() * scale, getFloatGreen() * scale,
                         getFloatBlue() * scale, getFloatAlpha());
  }
  Colour interpolatedWith(Colour other, float t) const {
    t = std::clamp(t, 0.f, 1.f);
    return fromFloatRGBA(
        getFloatRed() + (other.getFloatRed() - getFloatRed()) * t,
        getFloatGreen() + (other.getFloatGreen() - getFloatGreen()) * t,
        getFloatBlue() + (other.getFloatBlue() - getFloatBlue()) * t,
        getFloatAlpha() + (other.getFloatAlpha() - getFloatAlpha()) * t);
  }
  Colour darker(float amount = 1) const {
    return withBrightness(getBrightness() / (1 + amount));
  }
  Colour brighter(float amount = 1) const {
    return withBrightness(1 - (1 - getBrightness()) / (1 + amount));
  }
  Colour contrasting(float amount = 1) const {
    return interpolatedWith(
        Colour(getBrightness() >= 0.5f ? 0xff000000u : 0xffffffffu), amount);
  }
  Text toString() const { return Text::toHexString(argb_).paddedLeft('0', 8); }
  Text toDisplayString(bool alpha) const {
    return (alpha ? toString()
                  : Text::toHexString(argb_ & 0xffffff).paddedLeft('0', 6))
        .toUpperCase();
  }
  static Colour fromString(const Text &value) {
    return Colour(std::uint32_t(value.getHexValue64()));
  }
  friend bool operator==(Colour, Colour) = default;
};
namespace Colours {
inline constexpr Colour white{0xffffffff}, black{0xff000000},
    transparentBlack{0}, grey{0xff808080}, darkgrey{0xff555555},
    lightgrey{0xffd3d3d3}, red{0xffff0000}, green{0xff008000}, blue{0xff0000ff},
    orange{0xffffa500}, yellow{0xffffff00};
}
} // namespace cydaw::base
