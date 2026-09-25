#include "bootstrap.hpp"
#include <sdl-rdp/SDL3/rdp/backend/boundary.hpp>
#include <sdl-rdp/SDL3/rdp/owneddriver.hpp>
#include <sdl-rdp/SDL3/rdp/settings/options.hpp>
#include <sdl-rdp/SDL3/rdp/storage/drive.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <cstddef>
#include <cstdint>
#include <utility>
namespace sdl3::rdp::audio::detail::bootstrap {
using sdl3::rdp::backend::Boundary;
using sdl3::rdp::backend::Operation;
using sdl3::rdp::backend::ScopedMutexLock;
using sdl3::rdp::settings::InvalidSetting;
using sdl3::rdp::settings::Text;
using sdl_rdp::settings::Settings;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::RAIIWrap;
namespace {
constexpr SDL_AudioSpec PlaybackSpec     { SDL_AUDIO_S16, 2, 44100 };
constexpr int           PeriodsPerSecond = 100;
constexpr int           BackendWaitMs    = 100;
auto PeriodFrames(int frequency) -> int {
  return frequency / PeriodsPerSecond;
}
auto AudioLead(Driver const& driver) -> std::uint64_t {
  auto const lead = driver.Options().Value<&Settings::audio_lead>().Get();
  if (std::cmp_greater_equal(lead, driver.Config().audio_latency_ms)) InvalidSetting<LeadTooLong>();
  return static_cast<std::uint64_t>(lead) * SDL_NS_PER_MS;
}
auto OpenAudio(Driver const& driver) -> std::reference_wrapper<Driver const> {
  if (driver.Call<Operation::AUDIO_OPEN>() < 0) driver.Throw();
  return std::cref(driver);
}
auto CloseAudio(std::reference_wrapper<Driver const> driver) noexcept -> void {
  driver.get().Call<Operation::AUDIO_CLOSE>();
}
using AudioSession = RAIIWrap<std::reference_wrapper<Driver const>, OpenAudio, CloseAudio>;
}
}

using sdl3::rdp::Driver;
using sdl3::rdp::OwnedDriver;
using sdl3::rdp::audio::detail::bootstrap::AudioLead;
using sdl3::rdp::audio::detail::bootstrap::AudioSession;
using sdl3::rdp::backend::Operation;

// SDL declares this tag as a struct; the members stay private.
struct SDL_PrivateAudioData : private OwnedDriver<Driver const> {
public:
  using OwnedDriver<Driver const>::Backend;
  explicit SDL_PrivateAudioData(std::shared_ptr<Driver const> driver)
      : OwnedDriver<Driver const>{ std::move(driver) }, _lead{ AudioLead(Backend()) },
        _rate{ Backend().Call<Operation::AUDIO_RATE>() }, _session{ Backend() } { }
  auto     Buffer() -> std::vector<std::uint8_t>& {
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
  std::vector<std::uint8_t> _buffer;
  std::uint64_t             _next   { SDL_GetTicksNS() };
  std::uint64_t             _lead;
  std::uint32_t             _rate;
  AudioSession const        _session;
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
auto AwaitBackend(SDL_AudioDevice& device) -> bool {
  auto const& driver = device.hidden->Backend();
  int         result { };
  do {
    result = driver.Call<Operation::AUDIO_WAIT>(BackendWaitMs);
  } while (!result && !SDL_GetAtomicInt(&device.shutdown));
  return result >= 0 || driver.Fail();
}
// Without video nothing else drains the backend's event queue, so rate changes are polled here.
auto PollRateChanges(Driver const& driver) -> void {
  if (SDL_WasInit(SDL_INIT_VIDEO)) return;
  driver.Poll([](sdlrdp_event const& event) {
    if (event.type == SDLRDP_AUDIO) AudioRate(event.audio.freq);
  });
}
auto PlaybackDelay(SDL_AudioDevice& device) -> std::uint64_t {
  ScopedMutexLock const lock{ *device.lock };
  return device.hidden->Delay(device);
}
// SDL borrows the device while its audio thread waits for playback.
auto WaitDevice(SDL_AudioDevice* device) -> bool {
  Expects(device != nullptr, "audio wait has a device");
  return Boundary([&] {
    if (!AwaitBackend(*device)) return false;
    PollRateChanges(device->hidden->Backend());
    if (auto const delay = PlaybackDelay(*device)) SDL_DelayNS(delay);
    return true;
  });
}
// SDL audio playback provides a borrowed device and counted sample buffer.
auto PlayDevice(SDL_AudioDevice* device, std::uint8_t const* buffer, int length) -> bool {
  Expects(device != nullptr, "audio playback has a device");
  Expects(buffer != nullptr, "audio playback has a buffer");
  Expects(length >= 0, "audio buffer length is nonnegative");
  auto const  frames = Narrowed<std::uint32_t>(length) / SDL_AUDIO_FRAMESIZE(device->spec);
  auto const& driver = device->hidden->Backend();
  return std::cmp_equal(driver.Call<Operation::AUDIO_WRITE>(buffer, frames), frames) || driver.Fail();
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
