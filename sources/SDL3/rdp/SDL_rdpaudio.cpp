#include "SDL_rdpaudio.hpp"
#include "SDL_rdpdrive.hpp"
#include "boundary.hpp"
namespace rdp {
namespace {
constexpr SDL_AudioSpec PlaybackSpec     { SDL_AUDIO_S16, 2, 44100 };
constexpr int           PeriodsPerSecond = 100;
constexpr int           BackendWaitMs    = 100;
constexpr int           DefaultLeadMs    = 150;
auto PeriodFrames(int frequency) -> int {
  return frequency / PeriodsPerSecond;
}
auto AudioLead(Driver const& driver) -> Uint64 {
  auto const lead = driver.Options().Integer(SDL_HINT_RDP_AUDIO_LEAD, DefaultLeadMs, 0, SDL_MAX_SINT32);
  if (std::cmp_greater_equal(lead, driver.Config().audio_latency_ms))
    InvalidSetting("RDP audio lead must be below the audio latency window");
  return static_cast<Uint64>(lead) * SDL_NS_PER_MS;
}
auto OpenAudio(Driver const& driver) -> std::reference_wrapper<Driver const> {
  if (driver.Call<Operation::AUDIO_OPEN>() < 0) driver.Throw();
  return std::cref(driver);
}
auto CloseAudio(std::reference_wrapper<Driver const> driver) noexcept -> void {
  driver.get().Call<Operation::AUDIO_CLOSE>();
}
using AudioSession = utilities::RAIIWrap<std::reference_wrapper<Driver const>, OpenAudio, CloseAudio>;
}
}
// SDL declares this tag as a struct; the members stay private.
struct SDL_PrivateAudioData {
public:
  explicit SDL_PrivateAudioData(std::shared_ptr<rdp::Driver const> driver)
      : _driver{ std::move(driver) }, _lead{ rdp::AudioLead(*_driver) },
        _rate{ _driver->Call<rdp::Operation::AUDIO_RATE>() }, _session{ *_driver } { }
  auto     Backend() const -> rdp::Driver const& {
    return *_driver;
  }
  auto Buffer() -> std::vector<Uint8>& {
    return _buffer;
  }
  auto Rate() const -> unsigned {
    return _rate;
  }
  auto Rate(unsigned rate) -> void {
    if (rate && !_rate) _next = SDL_GetTicksNS();
    _rate = rate;
  }
  auto Delay(SDL_AudioDevice const& device) -> Uint64 {
    auto const now = SDL_GetTicksNS();
    _next += static_cast<Uint64>(device.sample_frames) * SDL_NS_PER_SECOND / static_cast<Uint64>(device.spec.freq);
    auto const delay = _next > now + _lead ? _next - now - _lead : 0;
    _next = std::max(_next, now);
    return delay;
  }
private:
  std::shared_ptr<rdp::Driver const> _driver;
  std::vector<Uint8>                 _buffer;
  Uint64                             _next   { SDL_GetTicksNS() };
  Uint64                             _lead;
  unsigned                           _rate;
  rdp::AudioSession const            _session;
};
namespace rdp {
namespace {
// SDL audio discovery returns borrowed device pointers through C out parameters.
auto DetectDevices(SDL_AudioDevice** playback, [[maybe_unused]] SDL_AudioDevice** unused_recording) -> void {
  utilities::Expects(playback != nullptr, "audio discovery has a playback output");
  *playback = SDL_AddAudioDevice(false, "RDP client", &PlaybackSpec, reinterpret_cast<void*>(DetectDevices));
}
// SDL lends the audio device to the open callback and owns its hidden state until close.
auto OpenDevice(SDL_AudioDevice* device) -> bool {
  utilities::Expects(device != nullptr, "audio open has a device");
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
auto ChangeRate(SDL_AudioDevice& device, unsigned rate) -> bool {
  auto& data = *device.hidden;
  data.Rate(rate);
  if (!rate || std::cmp_equal(rate, device.spec.freq)) return true;
  auto spec = device.spec;
  spec.freq = static_cast<int>(rate);
  if (!SDL_AudioDeviceFormatChangedAlreadyLocked(&device, &spec, PeriodFrames(spec.freq))) return false;
  try {
    data.Buffer().resize(static_cast<std::size_t>(device.buffer_size));
  } catch (std::bad_alloc const&) {
    return SDL_OutOfMemory();
  }
  return true;
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
auto AudioRate(unsigned rate) -> void {
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
auto PlaybackDelay(SDL_AudioDevice& device) -> Uint64 {
  ScopedMutexLock const lock{ *device.lock };
  return device.hidden->Delay(device);
}
// SDL borrows the device while its audio thread waits for playback.
auto WaitDevice(SDL_AudioDevice* device) -> bool {
  utilities::Expects(device != nullptr, "audio wait has a device");
  return Boundary([&] {
    if (!AwaitBackend(*device)) return false;
    PollRateChanges(device->hidden->Backend());
    if (auto const delay = PlaybackDelay(*device)) SDL_DelayNS(delay);
    return true;
  });
}
// SDL audio playback provides a borrowed device and counted sample buffer.
auto PlayDevice(SDL_AudioDevice* device, Uint8 const* buffer, int length) -> bool {
  utilities::Expects(device != nullptr, "audio playback has a device");
  utilities::Expects(buffer != nullptr, "audio playback has a buffer");
  utilities::Expects(length >= 0, "audio buffer length is nonnegative");
  auto const  frames = static_cast<unsigned>(length) / SDL_AUDIO_FRAMESIZE(device->spec);
  auto const& driver = device->hidden->Backend();
  return driver.Call<Operation::AUDIO_WRITE>(buffer, frames) == static_cast<int>(frames) || driver.Fail();
}
// SDL borrows the returned mixing buffer until its next device callback.
auto GetDeviceBuffer(SDL_AudioDevice* device, [[maybe_unused]] int* unused_size) -> Uint8* {
  utilities::Expects(device != nullptr, "audio buffer has a device");
  auto& buffer = device->hidden->Buffer();
  return std::cmp_greater_equal(buffer.size(), device->buffer_size) ? buffer.data() : nullptr;
}
// SDL returns ownership of hidden audio state to its close callback.
auto CloseDevice(SDL_AudioDevice* device) -> void {
  utilities::Expects(device != nullptr, "audio close has a device");
  std::unique_ptr<SDL_PrivateAudioData> const state{ std::exchange(device->hidden, nullptr) };
}
// SDL passes its writable audio callback table to bootstrap initialization.
auto InitAudio(SDL_AudioDriverImpl* implementation) -> bool {
  utilities::Expects(implementation != nullptr, "audio initialization has a callback table");
  implementation->DetectDevices = DetectDevices;
  implementation->OpenDevice    = OpenDevice;
  implementation->CloseDevice   = CloseDevice;
  implementation->WaitDevice    = WaitDevice;
  implementation->PlayDevice    = PlayDevice;
  implementation->GetDeviceBuf  = GetDeviceBuffer;
  return true;
}
}
}
// SDL's C bootstrap table requires this named object with static storage.
extern "C" AudioBootStrap const RDPAUDIO_bootstrap = { "rdp", "SDL RDP audio driver", rdp::InitAudio, true, false };
