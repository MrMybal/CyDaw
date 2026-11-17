#pragma once
#include "File.h"
namespace cydaw::base {
class InflateStream final : public InputStream {
public:
  enum Format { zlibFormat, deflateFormat, gzipFormat };
  InflateStream(InputStream *, bool deleteSource = false, Format = zlibFormat);
  ~InflateStream() override;
  int read(void *, int) override;
  int64 getPosition() override;
  int64 getTotalLength() override;
  bool setPosition(int64) override;
  bool isExhausted() override;
  Result getStatus() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
struct Base64 {
  static bool convertFromBase64(OutputStream &out, const Text &text) {
    MemoryBlock decoded;
    return decoded.fromBase64Encoding(text) &&
           out.write(decoded.getData(), decoded.getSize());
  }
  static Text toBase64(const void *data, std::size_t size) {
    return MemoryBlock(data, size).toBase64Encoding();
  }
};
} // namespace cydaw::base
