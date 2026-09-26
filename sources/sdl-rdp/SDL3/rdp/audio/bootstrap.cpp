#include "bootstrap.hpp"
#include <oxbox/utilities/span.hpp>
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/boundary.hpp>
#include <sdl-rdp/SDL3/rdp/settings/options.hpp>
#include <sdl-rdp/SDL3/rdp/storage/drive.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <variant>
namespace sdl3::rdp::audio::detail::bootstrap {
using sdl3::rdp::sdl::Boundary;
using sdl3::rdp::sdl::ScopedMutexLock;
using sdl3::rdp::settings::InvalidSetting;
using sdl3::rdp::settings::Text;
using sdl_rdp::link::AudioChanged;
using sdl_rdp::settings::Settings;
using sdl_rdp::utilities::DeadlineAfter;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::RAIIWrap;
namespace {
constexpr SDL_AudioSpec             PlaybackSpec     { SDL_AUDIO_S16, 2, 44100 };
constexpr int                       PeriodsPerSecond = 100;
constexpr std::chrono::milliseconds BackendWait      { 100                     };
auto PeriodFrames(int frequency) -> int {
  return frequency / PeriodsPerSecond;
}
auto AudioLead(Driver const& driver) -> std::uint64_t {
  auto const lead = driver.Options().Value<&Settings::audio_lead>().Get();
  if (std::cmp_greater_equal(lead, driver.Config().audio_latency_ms)) InvalidSetting<LeadTooLong>();
  return static_cast<std::uint64_t>(lead) * SDL_NS_PER_MS;
}
auto OpenAudio(Driver& driver) -> std::reference_wrapper<Driver> {
  driver.Backend().Audio().Open();
  return std::ref(driver);
}
auto CloseAudio(std::reference_wrapper<Driver> driver) noexcept -> void {
  Boundary([&] { driver.get().Backend().Audio().Close(); });
}
using AudioSession = RAIIWrap<std::reference_wrapper<Driver>, OpenAudio, CloseAudio>;
}
}

using sdl3::rdp::Driver;
using sdl3::rdp::audio::detail::bootstrap::AudioLead;
using sdl3::rdp::audio::detail::bootstrap::AudioSession;

// SDL declares this tag as a struct; the members stay private.
struct SDL_PrivateAudioData {
public:
  explicit SDL_PrivateAudioData(std::shared_ptr<sdl3::rdp::Driver> driver)
      : _driver{ std::move(driver) }, _lead{ AudioLead(*_driver) }, _rate{ _driver->Backend().Audio().Rate() },
        _session{ *_driver } { }
  auto     Driver() noexcept -> sdl3::rdp::Driver& {
    return *_driver;
  }
  auto Buffer() -> std::vector<std::uint8_t>& {
    return _buffer;
  }
  auto Rate() const -> std::uint32_t {
    return _rate;
  }
  auto Rate(std::uint32_t rate) -> void {
    if (rate && !_rate) _next = SDL_GetTicksNS();
    _rate = rate;
  }
  auto Delay(SDL_AudioDevice const& device) -> std::uint64_t {
    auto const now = SDL_GetTicksNS();
    _next += static_cast<std::uint64_t>(device.sample_frames) * SDL_NS_PER_SECOND
             / static_cast<std::uint64_t>(device.spec.freq);
    auto const delay = _next > now + _lead ? _next - now - _lead : 0;
    _next = std::max(_next, now);
    return delay;
  }
private:
  std::shared_ptr<sdl3::rdp::Driver> _driver;
  std::vector<std::uint8_t>          _buffer;
  std::uint64_t                      _next   { SDL_GetTicksNS() };
  std::uint64_t                      _lead;
  std::uint32_t                      _rate;
  AudioSession const                 _session;
};
namespace sdl3::rdp::audio::detail::bootstrap {
namespace {
// SDL audio discovery returns borrowed device pointers through C out parameters.
auto DetectDevices(SDL_AudioDevice** playback, [[maybe_unused]] SDL_AudioDevice** unused_recording) -> void {
  Expects(playback != nullptr, "audio discovery has a playback output");
  *playback = SDL_AddAudioDevice(false, "RDP client", &PlaybackSpec, reinterpret_cast<void*>(DetectDevices));
}
// SDL lends the audio device to the open callback and owns its hidden state until close.
auto OpenDevice(SDL_AudioDevice* device) -> bool {
  Expects(device != nullptr, "audio open has a device");
  return Boundary([&] {
    auto state = std::make_unique<SDL_PrivateAudioData>(Rendezvous::Acquire());
    device->spec = PlaybackSpec;
    if (state->Rate()) device->spec.freq = static_cast<int>(state->Rate());
    device->sample_frames = PeriodFrames(device->spec.freq);
    SDL_UpdatedAudioDeviceFormat(device);
    state->Buffer().resize(static_cast<std::size_t>(device->buffer_size));
    device->hidden = state.release();
    return true;
  });
}
auto ChangeRate(SDL_AudioDevice& device, std::uint32_t rate) -> bool {
  auto& data = *device.hidden;
  data.Rate(rate);
  if (!rate || std::cmp_equal(rate, device.spec.freq)) return true;
  auto spec = device.spec;
  spec.freq = static_cast<int>(rate);
  if (!SDL_AudioDeviceFormatChangedAlreadyLocked(&device, &spec, PeriodFrames(spec.freq))) return false;
  return Boundary([&] {
    data.Buffer().resize(static_cast<std::size_t>(device.buffer_size));
    return true;
  });
}
// SDL's audio registry identifies this driver's device by its discovery callback address.
auto PlaybackDevice() -> std::optional<std::reference_wrapper<SDL_AudioDevice>> {
  auto const current = Text(SDL_GetCurrentAudioDriver());
  if (current != "rdp") return std::nullopt;
  auto* const device = SDL_FindPhysicalAudioDeviceByHandle(reinterpret_cast<void*>(DetectDevices));
  if (!device) return std::nullopt;
  return std::ref(*device);
}
}
auto AudioRate(std::uint32_t rate) -> void {
  auto const found = PlaybackDevice();
  if (!found) return;
  auto&                 device = found->get();
  ScopedMutexLock const lock   { *device.lock };
  if (device.hidden && !ChangeRate(device, rate)) SDL_AudioDeviceDisconnected(&device);
}
namespace {
auto AwaitBackend(SDL_AudioDevice& device) -> void {
  auto& audio = device.hidden->Driver().Backend().Audio();
  while (!audio.Wait(DeadlineAfter(BackendWait)) && !SDL_GetAtomicInt(&device.shutdown)) {
  }
}
// Without video nothing else drains the backend's event queue, so rate changes are polled here.
auto PollRateChanges(Driver& driver) -> void {
  if (SDL_WasInit(SDL_INIT_VIDEO)) return;
  for (auto const& event : driver.Backend().Events().Poll())
    if (auto const* const audio = std::get_if<AudioChanged>(&event)) AudioRate(audio->rate);
}
auto PlaybackDelay(SDL_AudioDevice& device) -> std::uint64_t {
  ScopedMutexLock const lock{ *device.lock };
  return device.hidden->Delay(device);
}
// SDL borrows the device while its audio thread waits for playback.
auto WaitDevice(SDL_AudioDevice* device) -> bool {
  Expects(device != nullptr, "audio wait has a device");
  return Boundary([&] {
    AwaitBackend(*device);
    PollRateChanges(device->hidden->Driver());
    if (auto const delay = PlaybackDelay(*device)) SDL_DelayNS(delay);
    return true;
  });
}
// SDL audio playback provides a borrowed device and counted sample buffer.
auto PlayDevice(SDL_AudioDevice* device, std::uint8_t const* buffer, int length) -> bool {
  Expects(device != nullptr, "audio playback has a device");
  Expects(buffer != nullptr, "audio playback has a buffer");
  Expects(length >= 0, "audio buffer length is nonnegative");
  auto const bytes   = std::span(buffer, Narrowed<std::size_t>(length));
  auto const samples = oxbox::utilities::SpanCast<std::int16_t const>(bytes);
  auto const frames  = bytes.size() / Narrowed<std::size_t>(SDL_AUDIO_FRAMESIZE(device->spec));
  return Boundary([&] {
    auto const written = device->hidden->Driver().Backend().Audio().Write(samples);
    return written == frames || SDL_SetError("RDP audio accepted %zu of %zu frames", written, frames);
  });
}
// SDL borrows the returned mixing buffer until its next device callback.
auto GetDeviceBuffer(SDL_AudioDevice* device, [[maybe_unused]] int* unused_size) -> std::uint8_t* {
  Expects(device != nullptr, "audio buffer has a device");
  auto& buffer = device->hidden->Buffer();
  return std::cmp_greater_equal(buffer.size(), device->buffer_size) ? buffer.data() : nullptr;
}
// SDL returns ownership of hidden audio state to its close callback.
auto CloseDevice(SDL_AudioDevice* device) -> void {
  Expects(device != nullptr, "audio close has a device");
  std::unique_ptr<SDL_PrivateAudioData> const state{ std::exchange(device->hidden, nullptr) };
}
// SDL passes its writable audio callback table to bootstrap initialization.
auto InitAudio(SDL_AudioDriverImpl* implementation) -> bool {
  Expects(implementation != nullptr, "audio initialization has a callback table");
  implementation->DetectDevices = DetectDevices;
  implementation->OpenDevice    = OpenDevice;
  implementation->CloseDevice   = CloseDevice;
  implementation->WaitDevice    = WaitDevice;
  implementation->PlayDevice    = PlayDevice;
  implementation->GetDeviceBuf  = GetDeviceBuffer;
  return true;
}
}
// SDL's C bootstrap table requires this named object with static storage; C linkage names the global symbol.
extern "C" AudioBootStrap const RDPAUDIO_bootstrap = { "rdp", "SDL RDP audio driver", InitAudio, true, false };
}
