#include <sdl-rdp/freerdp-facade/wait-handle.hpp>

#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/utilities/parameter.hpp>

#include <freerdp/channels/wtsvc.h>
#include <winpr/synch.h>
#include <winpr/wtsapi.h>
#include <algorithm>
#include <memory>
#include <ranges>
#include <type_traits>

namespace sdl_rdp::freerdp_facade::detail::wait_handle {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Parameter;
using sdl_rdp::utilities::Releases;

namespace {
using QueriedMemory = std::unique_ptr<void, Releases<WTSFreeMemory>>;
// abi: WinPR's DWORD is unsigned long under Windows and std::uint32_t elsewhere.
using QueriedSize = std::remove_pointer_t<Parameter<WTSVirtualChannelQuery, 3>>;
static_assert(MaximumWaitHandles == MAXIMUM_WAIT_OBJECTS);
static_assert(Forever == INFINITE);
}
WaitHandle::WaitHandle(EventHandle const& event) : WaitHandle{ Adopted(event._event.get()) } { }
auto WaitHandle::Of(ServerHandle const& manager) -> WaitHandle {
  Expects(manager != nullptr, "the channel manager is open");
  return Adopted(WTSVirtualChannelManagerGetEventHandle(manager.get()));
}
auto WaitHandle::Of(ChannelHandle const& channel) -> WaitHandle {
  Expects(channel != nullptr, "the virtual channel is open");
  void*       data = nullptr;
  QueriedSize size = 0;
  if (!WTSVirtualChannelQuery(channel.get(), WTSVirtualEventHandle, &data, &size)) throw ChannelQueryFailed{ };
  QueriedMemory const queried{ data };
  Ensures(size == sizeof(HANDLE), "WinPR answers the event query with one handle");
  return Adopted(*static_cast<HANDLE const*>(queried.get()));
}
auto WaitHandle::Any(std::span<WaitHandle const> handles, std::uint32_t timeout) -> std::optional<std::size_t> {
  Expects(!handles.empty(), "a wait has a handle");
  Expects(handles.size() <= MaximumWaitHandles, "WinPR waits on at most MAXIMUM_WAIT_OBJECTS handles");
  Expects(!std::ranges::contains(handles, WaitHandle{ }), "every awaited handle is present");
  std::array<HANDLE, MaximumWaitHandles> natives{ };
  std::ranges::copy(handles | std::views::transform(&WaitHandle::_native), natives.begin());
  auto const result = WaitForMultipleObjects(Narrowed<std::uint32_t>(handles.size()), natives.data(), false, timeout);
  if (result == WAIT_FAILED) throw EventWaitFailed{ };
  if (result == WAIT_TIMEOUT) return std::nullopt;
  Ensures(result - WAIT_OBJECT_0 < handles.size(), "WinPR woke on an awaited handle");
  return result - WAIT_OBJECT_0;
}
auto WaitHandle::Adopted(void* native) -> WaitHandle {
  Expects(native != nullptr, "a waitable handle exists");
  Expects(native != INVALID_HANDLE_VALUE, "a waitable handle is valid");
  WaitHandle adopted;
  adopted._native = native;
  return adopted;
}
auto WaitHandle::Signalled() const -> bool {
  Expects(_native != nullptr, "a signalled check has a handle");
  auto const result = WaitForSingleObject(_native, 0);
  if (result == WAIT_FAILED) throw EventWaitFailed{ };
  return result == WAIT_OBJECT_0;
}
}
