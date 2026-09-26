#include <sdl-rdp/freerdp-facade/signalled.hpp>

#include <algorithm>
#include <iterator>

namespace sdl_rdp::freerdp_facade::detail::signalled {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;

Signalled::Signalled(std::span<WaitHandle const> handles) {
  Expects(handles.size() <= MaximumWaitHandles, "a wait has at most MAXIMUM_WAIT_OBJECTS handles");
  auto const copied = std::ranges::copy_if(handles, _fired.begin(), &WaitHandle::Signalled);
  _count = Narrowed<std::size_t>(std::ranges::distance(_fired.begin(), copied.out));
}
auto Signalled::Contains(WaitHandle handle) const -> bool {
  return std::ranges::contains(std::span{ _fired }.first(_count), handle);
}
auto Signalled::Contains(std::optional<WaitHandle> handle) const -> bool {
  return handle && Contains(*handle);
}
}
