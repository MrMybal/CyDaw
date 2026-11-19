#pragma once

#include <memory>
#include <string>

namespace cydaw::v2 {
class Studio;
class RemoteUi;

class Assistant final {
public:
    Assistant(Studio&, RemoteUi&);
    ~Assistant();
    void configure(int port, std::string token, bool allowEdits, int cacheSeconds, bool metrics);
    void draw(bool& open);
    void beginCheck(int providerPort);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cydaw::v2
