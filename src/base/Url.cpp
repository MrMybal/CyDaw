#include <cydaw/base/Url.h>
#ifdef _WIN32
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
// clang-format on
#endif
namespace cydaw::base {
Text Url::addEscapeChars(const Text &text, bool parameter) {
  std::string out;
  constexpr char hex[] = "0123456789ABCDEF";
  for (unsigned char c : text.toStdString()) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~' || (!parameter && (c == '/' || c == ':')))
      out += char(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}
Text Url::removeEscapeChars(const Text &text) {
  auto s = text.toStdString();
  std::string out;
  auto hex = [](char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
  };
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 &&
        hex(s[i + 2]) >= 0) {
      out += char((hex(s[i + 1]) << 4) | hex(s[i + 2]));
      i += 2;
    } else
      out += s[i];
  }
  return out;
}
Url::Url(const File &file) {
  auto path = file.getFullPathName().replaceCharacter('\\', '/');
  value_ = "file://" + (path.startsWith("/") ? Text{} : Text("/")) +
           addEscapeChars(path, false);
}
bool Url::launchInDefaultBrowser() const {
#ifdef _WIN32
  return reinterpret_cast<INT_PTR>(
             ShellExecuteW(nullptr, L"open", value_.toWideCharPointer(),
                           nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
  return false;
#endif
}
#ifdef _WIN32
namespace {
struct InternetHandle {
  HINTERNET value{};
  ~InternetHandle() {
    if (value)
      WinHttpCloseHandle(value);
  }
  InternetHandle() = default;
  InternetHandle(const InternetHandle &) = delete;
};
class HttpInput final : public InputStream {
public:
  InternetHandle session, connection, request;
  int64 position = 0, length = -1;
  bool exhausted = false;
  int read(void *data, int count) override {
    if (count <= 0 || exhausted)
      return 0;
    DWORD actual{};
    if (!WinHttpReadData(request.value, data, DWORD(count), &actual)) {
      exhausted = true;
      return -1;
    }
    position += actual;
    if (!actual)
      exhausted = true;
    return int(actual);
  }
  int64 getPosition() override { return position; }
  int64 getTotalLength() override { return length; }
  bool isExhausted() override {
    return exhausted || (length >= 0 && position >= length);
  }
  bool setPosition(int64 n) override { return n == position; }
};
} // namespace
#endif
std::unique_ptr<InputStream>
Url::createInputStream(const InputStreamOptions &options) const {
  if (options.status)
    *options.status = 0;
  if (options.headers)
    options.headers->clear();
#ifdef _WIN32
  if (!value_.startsWithIgnoreCase("https://") &&
      !value_.startsWithIgnoreCase("http://"))
    return {};
  auto text = value_.toWideString();
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
      DWORD(-1);
  if (!WinHttpCrackUrl(text.c_str(), DWORD(text.size()), 0, &parts))
    return {};
  auto stream = std::make_unique<HttpInput>();
  stream->session.value =
      WinHttpOpen(L"CyDaw/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!stream->session.value)
    return {};
  auto timeout = std::max(1, options.timeout);
  WinHttpSetTimeouts(stream->session.value, timeout, timeout, timeout, timeout);
  std::wstring host(parts.lpszHostName, parts.dwHostNameLength),
      path(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.dwExtraInfoLength)
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  if (path.empty())
    path = L"/";
  stream->connection.value =
      WinHttpConnect(stream->session.value, host.c_str(), parts.nPort, 0);
  if (!stream->connection.value)
    return {};
  stream->request.value = WinHttpOpenRequest(
      stream->connection.value, L"GET", path.c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
  if (!stream->request.value)
    return {};
  DWORD redirects = DWORD(std::max(0, options.redirects));
  if (!redirects) {
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(stream->request.value, WINHTTP_OPTION_REDIRECT_POLICY,
                     &policy, sizeof(policy));
  } else
    WinHttpSetOption(stream->request.value,
                     WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirects,
                     sizeof(redirects));
  auto extra = options.extra.toWideString();
  if (!WinHttpSendRequest(
          stream->request.value,
          extra.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extra.c_str(),
          DWORD(extra.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(stream->request.value, nullptr))
    return {};
  DWORD status{}, size = sizeof(status);
  WinHttpQueryHeaders(stream->request.value,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                      WINHTTP_NO_HEADER_INDEX);
  if (options.status)
    *options.status = int(status);
  size = 0;
  WinHttpQueryHeaders(stream->request.value, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                      WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                      WINHTTP_NO_HEADER_INDEX);
  if (size) {
    std::vector<wchar_t> raw(size / sizeof(wchar_t) + 1);
    if (WinHttpQueryHeaders(stream->request.value,
                            WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &size,
                            WINHTTP_NO_HEADER_INDEX)) {
      for (auto line : TextList::fromLines(Text(raw.data()))) {
        auto colon = line.indexOfChar(':');
        if (colon > 0) {
          auto key = line.substring(0, colon).trim(),
               value = line.substring(colon + 1).trim();
          if (options.headers)
            options.headers->set(key, value);
          if (key.equalsIgnoreCase("Content-Length"))
            stream->length = value.getLargeIntValue();
        }
      }
    }
  }
  return stream;
#else
  return {};
#endif
}
} // namespace cydaw::base
