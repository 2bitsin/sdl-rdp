#include "SDL_rdpvideo.hpp"
#include "boundary.hpp"
namespace rdp {
namespace {
constexpr auto TextMimeTypes = std::to_array({ "text/plain;charset=utf-8" });
// SDL takes ownership of the SDL-allocated string this produces.
auto CopyText(std::string const& text) -> char* {
  return SDL_strdup(text.c_str());
}
using ClipboardText = Resource<char*, CopyText, SDL_free>;
// SDL's clipboard callback borrows its device and a null-terminated string.
auto SetText(SDL_VideoDevice* device, char const* text) -> bool {
  utilities::Expects(device != nullptr, "clipboard write has a device");
  utilities::Expects(text != nullptr, "clipboard write has text");
  auto const& driver = device->internal->Backend();
  return driver.Call<Operation::SET_CLIPBOARD_TEXT>(text) == 0 || driver.Fail();
}
// SDL takes ownership of the returned SDL-allocated clipboard string.
auto GetText(SDL_VideoDevice* device) -> char* {
  utilities::Expects(device != nullptr, "clipboard read has a device");
  auto const& driver = device->internal->Backend();
  return Boundary([&] {
    auto const text = Text(driver.Call<Operation::GET_CLIPBOARD_TEXT>());
    if (!text) driver.Throw();
    return ClipboardText{ *text }.Release();
  });
}
// SDL's clipboard predicate borrows its device.
auto HasText(SDL_VideoDevice* device) -> bool {
  utilities::Expects(device != nullptr, "clipboard predicate has a device");
  auto const& driver = device->internal->Backend();
  auto const  result = driver.Call<Operation::HAS_CLIPBOARD_TEXT>();
  return result < 0 ? driver.Fail() : result != 0;
}
}
auto InitClipboard(SDL_VideoDevice& device) -> void {
  device.SetClipboardText = SetText;
  device.GetClipboardText = GetText;
  device.HasClipboardText = HasText;
}
auto ClipboardUpdate(SDL_VideoData const& data) -> void {
  auto const  count = data.Backend().Call<Operation::HAS_CLIPBOARD_TEXT>() > 0 ? TextMimeTypes.size() : 0;
  // Temporary memory: SDL frees the table itself once the update event is consumed.
  auto* const types = SDL_CopyClipboardMimeTypes(TextMimeTypes.data(), count, true);
  if (types == nullptr) throw std::bad_alloc();
  SDL_SendClipboardUpdate(false, types, count);
}
}
