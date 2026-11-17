#pragma once
#include "File.h"
#include "TextMap.h"
namespace cydaw::base {
class Url {
  Text value_;

public:
  enum class ParameterHandling { inAddress };
  struct InputStreamOptions {
    int timeout = 30000, redirects = 5;
    int *status = nullptr;
    TextMap *headers = nullptr;
    Text extra;
    explicit InputStreamOptions(ParameterHandling) {}
    InputStreamOptions withConnectionTimeoutMs(int n) const {
      auto v = *this;
      v.timeout = n;
      return v;
    }
    InputStreamOptions withNumRedirectsToFollow(int n) const {
      auto v = *this;
      v.redirects = n;
      return v;
    }
    InputStreamOptions withStatusCode(int *n) const {
      auto v = *this;
      v.status = n;
      return v;
    }
    InputStreamOptions withResponseHeaders(TextMap *n) const {
      auto v = *this;
      v.headers = n;
      return v;
    }
    InputStreamOptions withExtraHeaders(const Text &n) const {
      auto v = *this;
      v.extra = n;
      return v;
    }
  };
  explicit Url(Text v) : value_(std::move(v)) {}
  explicit Url(const File &);
  Text toString(bool = true) const { return value_; }
  bool launchInDefaultBrowser() const;
  std::unique_ptr<InputStream>
  createInputStream(const InputStreamOptions &) const;
  static Text addEscapeChars(const Text &, bool parameter);
  static Text removeEscapeChars(const Text &);
};
} // namespace cydaw::base
