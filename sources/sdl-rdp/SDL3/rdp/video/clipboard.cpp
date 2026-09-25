#include "clipboard.hpp"
#include <sdl-rdp/SDL3/rdp/sdl/boundary.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/resources.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <new>
#include <optional>
namespace sdl3::rdp::video::detail::clipboard {
using sdl3::rdp::sdl::Boundary;
using sdl3::rdp::sdl::Resource;
using sdl_rdp::utilities::Expects;
namespace {
constexpr auto TextMimeTypes = std::to_array({ "text/plain;charset=utf-8" });
using ClipboardText = Resource<char*, SDL_strdup, SDL_free>;
// SDL's clipboard callback borrows its device and a null-terminated string.
auto SetText(SDL_VideoDevice* device, char const* text) -> bool {
  Expects(device != nullptr, "clipboard write has a device");
  Expects(text != nullptr, "clipboard write has text");
  return Boundary([&] {
    device->internal->Driver().Backend().SetClipboardText(text);
    return true;
  });
}
// SDL takes ownership of the returned SDL-allocated clipboard string.
auto GetText(SDL_VideoDevice* device) -> char* {
  Expects(device != nullptr, "clipboard read has a device");
  auto copy = Boundary([&] -> std::optional<ClipboardText> {
    auto const text = device->internal->Driver().Backend().ClipboardText();
    return ClipboardText{ text.c_str() };
  });
  return copy ? copy->Release() : nullptr;
}
// SDL's clipboard predicate borrows its device.
auto HasText(SDL_VideoDevice* device) -> bool {
  Expects(device != nullptr, "clipboard predicate has a device");
  return Boundary([&] { return device->internal->Driver().Backend().HasClipboardText(); });
}
}
auto InitClipboard(SDL_VideoDevice& device) -> void {
  device.SetClipboardText = SetText;
  device.GetClipboardText = GetText;
  device.HasClipboardText = HasText;
}
auto ClipboardUpdate(SDL_VideoData& data) -> void {
  auto const count = data.Driver().Backend().HasClipboardText() ? TextMimeTypes.size() : 0;
  // Temporary memory: SDL frees the table itself once the update event is consumed.
  auto* const types = SDL_CopyClipboardMimeTypes(TextMimeTypes.data(), count, true);
  if (types == nullptr) throw std::bad_alloc();
  SDL_SendClipboardUpdate(false, types, count);
}
}
