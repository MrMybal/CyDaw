#include <climits>
#include <cydaw/base/Image.h>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "ysfx/thirdparty/stb/stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "ysfx/thirdparty/stb/stb_image_write.h"
namespace cydaw::base {
Image::Image(int width, int height) {
  if (width <= 0 || height <= 0 || width > 32768 || height > 32768 ||
      std::uint64_t(width) * height > 268435456)
    return;
  pixels_ = std::make_shared<Pixels>();
  pixels_->width = width;
  pixels_->height = height;
  pixels_->rgba.resize(std::size_t(width) * height * 4);
}
Colour Image::getPixelAt(int x, int y) const noexcept {
  if (!isValid() || x < 0 || y < 0 || x >= getWidth() || y >= getHeight())
    return {};
  auto *p = data() + std::size_t(y) * stride() + x * 4;
  return Colour::fromRGBA(p[0], p[1], p[2], p[3]);
}
void Image::setPixelAt(int x, int y, Colour colour) noexcept {
  if (!isValid() || x < 0 || y < 0 || x >= getWidth() || y >= getHeight())
    return;
  auto *p = writableData() + std::size_t(y) * stride() + x * 4;
  p[0] = colour.getRed();
  p[1] = colour.getGreen();
  p[2] = colour.getBlue();
  p[3] = colour.getAlpha();
}
Image Image::decode(const void *bytes, std::size_t size) {
  if (!bytes || size == 0 || size > INT_MAX)
    return {};
  int width{}, height{}, components{};
  if (!stbi_info_from_memory(static_cast<const stbi_uc *>(bytes), int(size),
                             &width, &height, &components))
    return {};
  Image image(width, height);
  if (!image.isValid())
    return {};
  auto *decoded =
      stbi_load_from_memory(static_cast<const stbi_uc *>(bytes), int(size),
                            &width, &height, &components, 4);
  if (!decoded)
    return {};
  std::memcpy(image.writableData(), decoded,
              std::size_t(image.stride()) * height);
  stbi_image_free(decoded);
  return image;
}
Image Image::rescaled(int width, int height) const {
  if (!isValid())
    return {};
  if (width == getWidth() && height == getHeight())
    return *this;
  Image result(width, height);
  if (!result.isValid())
    return {};
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const double sx = std::clamp((x + .5) * getWidth() / width - .5, 0.,
                                   double(getWidth() - 1));
      const double sy = std::clamp((y + .5) * getHeight() / height - .5, 0.,
                                   double(getHeight() - 1));
      const int x0 = int(sx), y0 = int(sy),
                x1 = std::min(x0 + 1, getWidth() - 1),
                y1 = std::min(y0 + 1, getHeight() - 1);
      const double fx = sx - x0, fy = sy - y0;
      const Colour values[] = {getPixelAt(x0, y0), getPixelAt(x1, y0),
                               getPixelAt(x0, y1), getPixelAt(x1, y1)};
      const double weights[] = {(1 - fx) * (1 - fy), fx * (1 - fy),
                                (1 - fx) * fy, fx * fy};
      double r{}, g{}, b{}, a{};
      for (int i = 0; i < 4; ++i) {
        const double w = weights[i] * values[i].getFloatAlpha();
        a += w;
        r += w * values[i].getFloatRed();
        g += w * values[i].getFloatGreen();
        b += w * values[i].getFloatBlue();
      }
      result.setPixelAt(x, y,
                        Colour::fromFloatRGBA(
                            a > 0 ? float(r / a) : 0, a > 0 ? float(g / a) : 0,
                            a > 0 ? float(b / a) : 0, float(a)));
    }
  return result;
}
bool Image::writePng(OutputStream &destination) const {
  if (!isValid())
    return false;
  struct Writer {
    OutputStream *output;
    bool okay = true;
  } writer{&destination};
  const auto callback = [](void *user, void *bytes, int size) {
    auto &w = *static_cast<Writer *>(user);
    if (w.okay)
      w.okay = w.output->write(bytes, std::size_t(size));
  };
  return stbi_write_png_to_func(callback, &writer, getWidth(), getHeight(), 4,
                                data(), stride()) != 0 &&
         writer.okay;
}
} // namespace cydaw::base
