#pragma once
#include "File.h"
#include "Geometry.h"
#include <memory>
#include <vector>
namespace cydaw::base {
// Shared immutable-at-publication, straight RGBA8 pixels. Writers finish before
// publishing a copy to the UI; bitmap rows never depend on platform channel
// order.
class Image {
  struct Pixels {
    int width{}, height{};
    std::vector<std::uint8_t> rgba;
  };
  std::shared_ptr<Pixels> pixels_;

public:
  Image() = default;
  Image(int width, int height);
  bool isValid() const noexcept { return pixels_ && !pixels_->rgba.empty(); }
  int getWidth() const noexcept { return pixels_ ? pixels_->width : 0; }
  int getHeight() const noexcept { return pixels_ ? pixels_->height : 0; }
  const std::uint8_t *data() const noexcept {
    return isValid() ? pixels_->rgba.data() : nullptr;
  }
  std::uint8_t *writableData() noexcept {
    return isValid() ? pixels_->rgba.data() : nullptr;
  }
  int stride() const noexcept { return getWidth() * 4; }
  Colour getPixelAt(int x, int y) const noexcept;
  void setPixelAt(int x, int y, Colour colour) noexcept;
  Image rescaled(int width, int height) const;
  static Image decode(const void *bytes, std::size_t size);
  bool writePng(OutputStream &destination) const;
};
} // namespace cydaw::base
