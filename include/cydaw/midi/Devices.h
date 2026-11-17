#pragma once
#include "EventBuffer.h"
#include <cyaudio/io/MidiDevice.h>
#include <cydaw/base/Thread.h>
#include <cydaw/base/Value.h>
namespace cydaw::midi {
struct DeviceInfo {
  base::Text name, identifier;
};
class Input;
class InputCallback {
public:
  virtual ~InputCallback() = default;
  virtual void handleIncomingMidiMessage(Input *, const Message &) = 0;
};
// Enumeration identifiers encode Core selectors, never a transient port index.
base::Array<DeviceInfo> enumerateDevices(bool input);
class Input final : private base::Thread {
  cyaudio::MidiInput device_;
  DeviceInfo info_;
  InputCallback *callback_{};
  Input(DeviceInfo info, InputCallback *cb)
      : Thread("CyDaw MIDI input"), info_(std::move(info)), callback_(cb) {}
  void run() override;

public:
  ~Input() override { stop(); }
  static base::Array<DeviceInfo> getAvailableDevices() {
    return enumerateDevices(true);
  }
  static std::unique_ptr<Input> openDevice(const base::Text &, InputCallback *);
  void start() { startThread(); }
  void stop() {
    signalThreadShouldExit();
    notify();
    stopThread(-1);
  }
  const base::Text &getIdentifier() const { return info_.identifier; }
  const base::Text &getName() const { return info_.name; }
};
class Output final {
  cyaudio::MidiOutput device_;
  DeviceInfo info_;
  std::mutex sendLock_;

public:
  static base::Array<DeviceInfo> getAvailableDevices() {
    return enumerateDevices(false);
  }
  static std::unique_ptr<Output> openDevice(const base::Text &);
  const base::Text &getIdentifier() const { return info_.identifier; }
  const base::Text &getName() const { return info_.name; }
  void sendMessageNow(const Message &message) {
    std::lock_guard lock(sendLock_);
    (void)device_.send(
        {message.getRawData(), std::size_t(message.getRawDataSize())});
  }
  void sendBlockOfMessagesNow(const EventBuffer &events) {
    for (const auto event : events)
      sendMessageNow(event.getMessage());
  }
};
} // namespace cydaw::midi
