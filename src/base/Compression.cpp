#include <cydaw/base/Compression.h>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>
namespace cydaw::base {
struct InflateStream::Impl {
  InputStream *source;
  std::unique_ptr<InputStream> owner;
  int64 origin;
  Format format;
  mz_stream stream{};
  std::array<unsigned char, 16384> input{};
  bool initialized = false, ended = false;
  int64 position = 0;
  unsigned long crc = 0;
  Result status;
  Impl(InputStream *in, bool own, Format type)
      : source(in), owner(own ? in : nullptr),
        origin(in ? in->getPosition() : 0), format(type) {
    reset();
  }
  ~Impl() {
    if (initialized)
      mz_inflateEnd(&stream);
  }
  bool fail(const char *message) {
    status = Result::fail(message);
    ended = true;
    return false;
  }
  int byte() {
    unsigned char c{};
    if (stream.avail_in) {
      c = *stream.next_in++;
      --stream.avail_in;
      return c;
    }
    return source && source->read(&c, 1) == 1 ? int(c) : -1;
  }
  bool header() {
    unsigned long checksum = 0;
    auto next = [&]() {
      int b = byte();
      if (b >= 0) {
        auto value = static_cast<unsigned char>(b);
        checksum = mz_crc32(checksum, &value, 1);
      }
      return b;
    };
    int a = next(), b = next(), method = next(), flags = next();
    if (a != 31 || b != 139 || method != 8 || flags < 0 || (flags & 0xe0))
      return fail("Invalid gzip header");
    for (int i = 0; i < 6; ++i)
      if (next() < 0)
        return fail("Truncated gzip header");
    if (flags & 4) {
      int lo = next(), hi = next();
      if (lo < 0 || hi < 0)
        return fail("Truncated gzip extra data");
      int n = lo | (hi << 8);
      while (n--)
        if (next() < 0)
          return fail("Truncated gzip extra data");
    }
    for (int flag : {8, 16})
      if (flags & flag) {
        bool terminated = false;
        for (int n = 0; n < 1024 * 1024; ++n) {
          int value = next();
          if (value < 0)
            return fail("Truncated gzip text");
          if (!value) {
            terminated = true;
            break;
          }
        }
        if (!terminated)
          return fail("Gzip text exceeds limit");
      }
    if (flags & 2) {
      auto expected = checksum & 65535;
      int lo = byte(), hi = byte();
      if (lo < 0 || hi < 0 || unsigned(lo | (hi << 8)) != expected)
        return fail("Gzip header checksum mismatch");
    }
    return true;
  }
  void reset() {
    if (initialized)
      mz_inflateEnd(&stream);
    stream = {};
    initialized = false;
    ended = false;
    position = 0;
    crc = 0;
    status = Result::ok();
    if (!source ||
        (source->getPosition() != origin && !source->setPosition(origin))) {
      fail("Cannot seek compressed source");
      return;
    }
    if (format == gzipFormat && !header())
      return;
    if (mz_inflateInit2(&stream, format == zlibFormat ? 15 : -15) != MZ_OK) {
      fail("Cannot initialize inflater");
      return;
    }
    initialized = true;
  }
  bool trailer() {
    std::uint32_t expectedCrc = 0, expectedSize = 0;
    for (unsigned i = 0; i < 8; ++i) {
      int value = byte();
      if (value < 0)
        return fail("Truncated gzip trailer");
      (i < 4 ? expectedCrc : expectedSize) |= std::uint32_t(value)
                                              << (8 * (i % 4));
    }
    return expectedCrc == crc && expectedSize == std::uint32_t(position)
               ? true
               : fail("Gzip data checksum mismatch");
  }
  int read(void *destination, int maximum) {
    if (!initialized || ended || maximum <= 0)
      return 0;
    stream.next_out = static_cast<unsigned char *>(destination);
    stream.avail_out = unsigned(maximum);
    auto *begin = stream.next_out;
    int produced = 0;
    while (stream.avail_out) {
      if (stream.avail_in == 0) {
        int count = source->read(input.data(), int(input.size()));
        stream.next_in = input.data();
        stream.avail_in = unsigned(std::max(0, count));
      }
      auto before = stream.avail_out;
      auto beforeInput = stream.avail_in;
      int result = mz_inflate(&stream, MZ_NO_FLUSH);
      auto count = before - stream.avail_out;
      crc = mz_crc32(crc, begin + produced, count);
      position += count;
      produced += int(count);
      if (result == MZ_STREAM_END) {
        ended = true;
        if (format == gzipFormat)
          trailer();
        break;
      }
      if (result != MZ_OK && result != MZ_BUF_ERROR) {
        fail("Invalid compressed stream");
        break;
      }
      if (count == 0 && beforeInput == stream.avail_in) {
        if (stream.avail_in < input.size()) {
          auto retained = stream.avail_in;
          std::memmove(input.data(), stream.next_in, retained);
          int more = source->read(input.data() + retained,
                                  int(input.size() - retained));
          if (more > 0) {
            stream.next_in = input.data();
            stream.avail_in = retained + unsigned(more);
            continue;
          }
        }
        fail("Truncated or stalled compressed stream");
        break;
      }
    }
    return produced;
  }
};
InflateStream::InflateStream(InputStream *input, bool own, Format format)
    : impl_(std::make_unique<Impl>(input, own, format)) {}
InflateStream::~InflateStream() = default;
int InflateStream::read(void *p, int n) { return impl_->read(p, n); }
int64 InflateStream::getPosition() { return impl_->position; }
int64 InflateStream::getTotalLength() {
  return impl_->ended ? impl_->position : -1;
}
bool InflateStream::isExhausted() { return impl_->ended; }
Result InflateStream::getStatus() const { return impl_->status; }
bool InflateStream::setPosition(int64 target) {
  if (target < 0)
    return false;
  if (target < impl_->position)
    impl_->reset();
  std::array<unsigned char, 16384> scratch;
  while (impl_->position < target) {
    int n =
        read(scratch.data(),
             int(std::min<int64>(scratch.size(), target - impl_->position)));
    if (n <= 0)
      return false;
  }
  return impl_->position == target && impl_->status.wasOk();
}
} // namespace cydaw::base
