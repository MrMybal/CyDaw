#include <cydaw/base/Listeners.h>
#include <cydaw/base/Platform.h>
#include <cydaw/base/Url.h>
#include <cydaw/base/Utilities.h>
#include <cydaw/base/Zip.h>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cydaw::base;
static void need(bool b, const char *why) {
  if (!b)
    throw std::runtime_error(why);
}
int main(int argc, char **argv) {
  try {
    auto arguments = cydaw::base::commandLineArguments(argc, argv);
    need(arguments.size() == 3, "HTTP and archive fixture paths required");
    need(Text::isValidUTF8("a\xc3\xa9\xf0\x9f\x8e\xac", 7), "valid UTF8");
    for (auto s :
         {std::string("\xc0\x80", 2), std::string("\xed\xa0\x80", 3),
          std::string("\xf4\x90\x80\x80", 4), std::string("\xe2\x82", 2)})
      need(!Text::isValidUTF8(s.data(), s.size()), "invalid UTF8 rejected");
    const char16_t utf16[] = {0x41, 0xd83c, 0xdfac, 0x00e9, 0};
    need(Text::fromUTF16(utf16, 4) ==
             Text::fromUTF8("A\xf0\x9f\x8e\xac\xc3\xa9"),
         "UTF16 surrogate pairs");
    need(SystemStats::getEnvironmentVariable("CYDAW_NATIVE_UTF8_TEST") ==
             Text::fromUTF8("fran\xc3\xa7"
                            "ais \xf0\x9f\x8e\xac"),
         "Windows UTF16 environment preserves Unicode paths");
    auto original = Text::fromUTF8("fran\xc3\xa7"
                                   "ais # + %/");
    need(Url::removeEscapeChars(Url::addEscapeChars(original, true)) ==
             original,
         "URL encoding lossless");
    auto url = Url(File::getSpecialLocation(File::tempDirectory)
                       .getChildFile(original + ".html"))
                   .toString();
    need(url.startsWith("file:///") && url.contains("%23") &&
             url.contains("%25"),
         "file URL escaping");
    InterProcessLock owner("CyDaw-platform-test");
    need(owner.enter(0), "first named lock");
    bool entered = true;
    std::thread contender([&] {
      InterProcessLock other("CyDaw-platform-test");
      entered = other.enter(0);
    });
    contender.join();
    need(!entered, "cross-thread exclusivity");
    owner.exit();
    std::thread next([&] {
      InterProcessLock other("CyDaw-platform-test");
      entered = other.enter(0);
    });
    next.join();
    need(entered, "named lock released");
    struct Listener {
      int calls = 0;
    };
    Listener a, b;
    ListenerList<Listener> listeners;
    listeners.add(&a);
    listeners.add(&b);
    listeners.call([&](Listener &item) {
      ++item.calls;
      if (&item == &a)
        listeners.remove(&b);
    });
    need(a.calls == 1 && b.calls == 0,
         "callback removal does not call dangling listeners");
    auto scanRoot = File(arguments[2]).getSiblingFile("lazy-scan");
    need(scanRoot.createDirectory().wasOk(), "create scan fixture");
    RangedDirectoryIterator lazy(scanRoot, true, "*.wav", File::findFiles);
    need(scanRoot.getChildFile("nested").createDirectory().wasOk(),
         "nested scan fixture");
    need(scanRoot.getChildFile("nested/later.wav").replaceWithText("fixture"),
         "write lazy entry");
    need(scanRoot.getChildFile("ignored.txt").replaceWithText("fixture"),
         "write excluded entry");
    int found = 0;
    for (const auto &entry : lazy) {
      need(entry.getFile().getFileName() == "later.wav",
           "recursive wildcard filter");
      ++found;
    }
    need(found == 1,
         "scan opens lazily and reads entries added after range construction");
    ZipFile zip{File(arguments[2])};
    need(zip.getNumEntries() == 3, "ZIP central directory");
    auto index = zip.getIndexOfFileName("media/large.bin");
    need(index >= 0, "ZIP path index");
    std::unique_ptr<InputStream> entry(zip.createStreamForEntry(index));
    need(entry && entry->getTotalLength() == 1048576, "ZIP stream length");
    std::array<unsigned char, 4093> data{};
    int64 total = 0;
    while (!entry->isExhausted()) {
      int n = entry->read(data.data(), int(data.size()));
      need(n > 0, "ZIP CRC and progress");
      for (int i = 0; i < n; ++i)
        need(data[size_t(i)] == unsigned((total + i) % 251),
             "ZIP streaming payload");
      total += n;
    }
    need(total == 1048576, "ZIP full extraction");
    need(zip.getIndexOfFileName(Text::fromUTF8("\xc3\xa9.xml")) >= 0,
         "ZIP Unicode entry");
    // A ZIP entry is never interpreted as an extraction destination.
    need(zip.getIndexOfFileName("../outside") >= 0,
         "archive names retained as metadata only");
    int status{};
    TextMap headers;
    auto options = Url::InputStreamOptions(Url::ParameterHandling::inAddress)
                       .withStatusCode(&status)
                       .withResponseHeaders(&headers)
                       .withConnectionTimeoutMs(3000);
    auto base = arguments[1];
    auto response = Url(base + "/redirect").createInputStream(options);
    need(response && status == 200 &&
             response->readEntireStreamAsString() == "native-download",
         "HTTP redirect and stream");
    need(headers.getValue("x-cydaw") == "native",
         "case-insensitive response headers");
    response =
        Url(base + "/data")
            .createInputStream(options.withExtraHeaders("Range: bytes=7-"));
    need(response && status == 206 &&
             response->readEntireStreamAsString() == "download",
         "HTTP range resume");
    response = Url(base + "/missing").createInputStream(options);
    need(response && status == 404, "HTTP errors preserved");
    response = Url(base + "/redirect")
                   .createInputStream(options.withNumRedirectsToFollow(0));
    need(response && status == 302, "redirect policy honored");
    std::cout << "Native platform services passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
