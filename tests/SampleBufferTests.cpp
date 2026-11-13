#include <cydaw/audio/SampleBuffer.h>
#include <iostream>
#include <stdexcept>
using cydaw::audio::SampleBuffer;
void require(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error(message);
}
int main() {
  try {
    SampleBuffer<float> owned(2, 256);
    require(reinterpret_cast<std::uintptr_t>(owned.getWritePointer(0)) % 64 ==
                0,
            "CyDSP sample storage is aligned");
    for (int ch = 0; ch < 2; ++ch)
      for (int i = 0; i < 256; ++i)
        owned.setSample(ch, i, float(ch + 1));
    auto *original = owned.getWritePointer(0);
    SampleBuffer<float> view(owned.getArrayOfWritePointers(), 2, 32, 64);
    view.applyGain(.5f);
    require(owned.getSample(0, 31) == 1 && owned.getSample(0, 32) == .5f &&
                owned.getSample(0, 95) == .5f && owned.getSample(0, 96) == 1,
            "borrowed subrange applies gain directly without touching "
            "surrounding samples");
    SampleBuffer<float> copy = view;
    copy.clear();
    require(owned.getSample(0, 32) == .5f && copy.getMagnitude(0, 64) == 0,
            "copy owns independent samples");
    owned.setSize(2, 128, true, false, true);
    require(owned.getWritePointer(0) == original && owned.getSample(1, 40) == 1,
            "prepared capacity is reused without allocation");
    owned.setSize(2, 256, true, true, true);
    require(owned.getSample(0, 200) == 0, "newly exposed tail is cleared");
    SampleBuffer<float> ramp(1, 4);
    for (int i = 0; i < 4; ++i)
      ramp.setSample(0, i, 1);
    ramp.applyGainRamp(0, 4, 0.f, 1.f);
    require(ramp.getSample(0, 0) == 0 && ramp.getSample(0, 3) == .75f,
            "sample ramp reaches endpoint at next block boundary");
    auto extrema = ramp.findMinMax(0, 0, 4);
    require(extrema.getStart() == 0 && extrema.getEnd() == .75f &&
                std::abs(ramp.getRMSLevel(0, 0, 4) - std::sqrt(.875f / 4)) <
                    1e-6f,
            "native extrema and RMS");
    ramp.copyFrom(0, 1, ramp, 0, 0, 3);
    require(ramp.getSample(0, 1) == 0 && ramp.getSample(0, 2) == .25f &&
                ramp.getSample(0, 3) == .5f,
            "overlapping copy preserves source samples");
    auto moved = std::move(ramp);
    require(moved.getSample(0, 3) == .5f,
            "aligned storage and pointer view move together");
    std::cout << "Native aligned sample buffers, borrowed views, ramps, copy "
                 "and preparation passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
