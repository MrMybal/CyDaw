#pragma once

#include <memory>
#include <optional>
#include <string>

namespace cydaw::v2 {
class Studio;
class RemoteUi;

struct AssistantOptions {
    int port{};
    int cacheSeconds{30};
    bool allowEdits{};
    bool metrics{};
    std::optional<std::string> profile;
};

class Assistant final {
public:
    Assistant(Studio&, RemoteUi&);
    ~Assistant();
    void configure(const AssistantOptions&);
    void draw(bool& open);
    void beginCheck(int providerPort);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cydaw::v2
