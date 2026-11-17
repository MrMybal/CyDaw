#include <cydaw/base/Compression.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw::base;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int main() {
  try {
    auto expected =
        Text::repeatedString("CyDaw standard compressed model rows.\n", 2048);
    struct Fixture {
      const char *encoded;
      InflateStream::Format format;
    };
    Fixture fixtures[] = {
        {"eJztyrENgCAQAMDeKZjAJXAR4tOpGDAhbu8YNnf15XcrM42nXFF6pL2dd69j1Ehni3qk3"
         "uZYl2xZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlm"
         "VZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmV"
         "ZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZ"
         "lmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZlmVZl"
         "mVZlmVZlmVZlmVZlmVZlmVZlmVZlmX9vD5iqZaC",
         InflateStream::zlibFormat},
        {"H4sIAAAAAAACCu3KsQ2AIBAAwN4pmMAlcBHi06kYMCFu7xg2d/"
         "XldyszjadcUXqkvZ13r2PUSGeLeqTe5liXbFmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVm"
         "WZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmW"
         "ZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZ"
         "VmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZV"
         "mWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZVmWZf28PqOLdos"
         "AMAEA",
         InflateStream::gzipFormat},
        {"7cqxDYAgEADA3imYwCVwEeLTqRgwIW7vGDZ39eV3KzONp1xReqS9nXevY9RIZ4t6pN7mW"
         "JdsWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZ"
         "ZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZ"
         "lWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZl"
         "WZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlWZZlW"
         "ZZlWZZlWZZlWZZlWZZlWZZlWZZl/bw+",
         InflateStream::deflateFormat},
    };
    for (auto fixture : fixtures) {
      MemoryBlock compressed;
      require(compressed.fromBase64Encoding(fixture.encoded),
              "external fixture base64");
      for (int quantum : {1, 17, 4096, 16384}) {
        MemoryInputStream source(compressed, false);
        InflateStream decoder(&source, false, fixture.format);
        MemoryOutputStream result;
        std::vector<char> buffer(quantum);
        for (;;) {
          int n = decoder.read(buffer.data(), quantum);
          if (n <= 0)
            break;
          result.write(buffer.data(), std::size_t(n));
        }
        require(
            decoder.getStatus().wasOk() && decoder.isExhausted() &&
                result.toString() == expected,
            "standard Python/zlib fixture decodes with every output quantum");
        require(decoder.setPosition(10), "compressed stream rewind");
        std::array<char, 20> section{};
        require(decoder.read(section.data(), 20) == 20 &&
                    Text::fromUTF8(section.data(), 20) ==
                        expected.substring(10, 30),
                "seek reads the correct decompressed region");
      }
      if (fixture.format != InflateStream::deflateFormat) {
        auto *bytes = static_cast<unsigned char *>(compressed.getData());
        bytes[compressed.getSize() - 1] ^= 0x55;
        MemoryInputStream source(compressed, false);
        InflateStream decoder(&source, false, fixture.format);
        MemoryBlock output;
        decoder.readIntoMemoryBlock(output, expected.getNumBytesAsUTF8() + 1);
        require(decoder.getStatus().failed(),
                "corrupt trailer/checksum rejected");
      }
    }
    std::cout << "Standard zlib/gzip/raw fixtures, chunk boundaries, rewind "
                 "and corruption passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
