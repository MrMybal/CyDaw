#pragma once
#include <cstdint>
#include <cydaw/engine/FoundrySession.h>

namespace cydaw::app
{
struct ModeInput
{
    std::uint64_t id{};
    const float *left{};
    const float *right{};
};
// Owned on the message thread and borrowed for one audio callback.
class ModeProcessor
{
  public:
    virtual ~ModeProcessor() = default;
    virtual void prepare(double rate, int block) = 0;
    virtual void process(const ModeInput *inputs, int count, float *left, float *right, int frames) = 0;
    virtual void processFoundry(const engine::foundry::Session &session, double at, double step, float *left, float *right, int frames) = 0;
};
} // namespace cydaw::app
