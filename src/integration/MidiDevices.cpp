#include <cydaw/midi/Devices.h>
namespace cydaw::midi {
namespace {
base::Text identifier(const cyaudio::MidiDevicePorts &ports,
                      const cyaudio::MidiPortInfo &port, bool input) {
  cyaudio::MidiPortSelector s{ports.backend,
                              port.persistentIdentity ? port.persistentId
                                                      : std::string{},
                              port.name, input, !port.persistentIdentity};
  std::string encoded;
  return cyaudio::serializeMidiPortSelector(s, encoded) == cyaudio::Status::ok
             ? base::Text(encoded)
             : base::Text{};
}
bool resolve(const base::Text &id, bool input, cyaudio::MidiDevicePorts &ports,
             std::uint32_t &index, DeviceInfo &info) {
  if (cyaudio::enumerateMidiPorts(ports) != cyaudio::Status::ok)
    return false;
  cyaudio::MidiPortSelector selector;
  if (cyaudio::parseMidiPortSelector(id.toStdString(), selector) !=
          cyaudio::Status::ok ||
      selector.input != input)
    return false;
  if (cyaudio::resolveMidiPort(ports, selector, index) != cyaudio::Status::ok)
    return false;
  for (const auto &port : input ? ports.inputs : ports.outputs)
    if (port.index == index) {
      info = {base::Text(port.name), identifier(ports, port, input)};
      return true;
    }
  return false;
}
} // namespace
base::Array<DeviceInfo> enumerateDevices(bool input) {
  base::Array<DeviceInfo> out;
  cyaudio::MidiDevicePorts ports;
  if (cyaudio::enumerateMidiPorts(ports) != cyaudio::Status::ok)
    return out;
  for (const auto &port : input ? ports.inputs : ports.outputs) {
    auto id = identifier(ports, port, input);
    if (id.isNotEmpty())
      out.add({base::Text(port.name), std::move(id)});
  }
  return out;
}
std::unique_ptr<Input> Input::openDevice(const base::Text &id,
                                         InputCallback *cb) {
  cyaudio::MidiDevicePorts ports;
  std::uint32_t index{};
  DeviceInfo info;
  if (!cb || !resolve(id, true, ports, index, info))
    return {};
  auto result = std::unique_ptr<Input>(new Input(std::move(info), cb));
  cyaudio::MidiInputOptions options;
  options.queuedMessages = 512;
  options.maximumMessageBytes = 65536;
  if (result->device_.open(index, options) != cyaudio::Status::ok)
    return {};
  return result;
}
void Input::run() {
  std::array<std::uint8_t, 65536> bytes;
  while (!threadShouldExit()) {
    std::size_t size{};
    cyaudio::MidiInputTimestamp stamp;
    const auto status = device_.readTimed(bytes, size, stamp);
    if (status == cyaudio::Status::ok) {
      callback_->handleIncomingMidiMessage(
          this, Message(bytes.data(), int(size),
                        double(stamp.arrivalNanoseconds) * 1e-9));
      continue;
    }
    if (status != cyaudio::Status::end_of_stream)
      break;
    wait(1);
  }
}
std::unique_ptr<Output> Output::openDevice(const base::Text &id) {
  cyaudio::MidiDevicePorts ports;
  std::uint32_t index{};
  DeviceInfo info;
  if (!resolve(id, false, ports, index, info))
    return {};
  auto result = std::make_unique<Output>();
  result->info_ = std::move(info);
  if (result->device_.open(index) != cyaudio::Status::ok)
    return {};
  return result;
}
} // namespace cydaw::midi
