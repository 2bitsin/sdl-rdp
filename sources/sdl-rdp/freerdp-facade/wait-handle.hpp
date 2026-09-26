#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <winpr/wtypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace sdl_rdp::freerdp_facade::detail::wait_handle {
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Narrowed;

inline constexpr std::size_t   MaximumWaitHandles = 64;
inline constexpr std::uint32_t Forever            = 0xFFFFFFFF;
// A waitable WinPR lends; the object it came from owns and closes it, a default one is no handle.
class WaitHandle {
public:
              WaitHandle() noexcept = default;
  explicit    WaitHandle(EventHandle const& event);
  static auto Of(ChannelManager const& manager) -> WaitHandle;
  static auto Of(VirtualChannel const& channel) -> WaitHandle;
  template <auto QUERY, class ContextTy> static auto Lent(ContextTy& context) -> WaitHandle;
  template <auto QUERY, class ContextTy> static auto Reported(ContextTy& context) -> std::optional<WaitHandle>;
  template <auto QUERY, class ContextTy>
  static auto Collected(ContextTy& context, std::span<WaitHandle> out)        -> std::span<WaitHandle>;
  static auto Any(std::span<WaitHandle const> handles, std::uint32_t timeout) -> std::optional<std::size_t>;
  auto        Signalled() const                                               -> bool;
  auto        operator==(WaitHandle const& other) const noexcept              -> bool = default;

private:
  static auto Adopted(HANDLE native) -> WaitHandle;
  template <auto QUERY, class ContextTy, class... ArgumentsTy>
  static auto Call(ContextTy& context, ArgumentsTy... arguments) -> decltype(auto);
  // isolated: WinPR's waitable is opaque, and nothing outside this class reads it.
  HANDLE _native{ };
};
template <auto QUERY, class ContextTy, class... ArgumentsTy>
auto WaitHandle::Call(ContextTy& context, ArgumentsTy... arguments) -> decltype(auto) {
  if constexpr (std::is_member_object_pointer_v<decltype(QUERY)>)
    return (context.*QUERY)(&context, arguments...);
  else
    return QUERY(&context, arguments...);
}
template <auto QUERY, class ContextTy> auto WaitHandle::Lent(ContextTy& context) -> WaitHandle {
  return Adopted(Call<QUERY>(context));
}
template <auto QUERY, class ContextTy> auto WaitHandle::Reported(ContextTy& context) -> std::optional<WaitHandle> {
  HANDLE reported{ };
  if (!Call<QUERY>(context, &reported)) return std::nullopt;
  return Adopted(reported);
}
template <auto QUERY, class ContextTy>
auto WaitHandle::Collected(ContextTy& context, std::span<WaitHandle> out) -> std::span<WaitHandle> {
  std::array<HANDLE, MaximumWaitHandles> natives { };
  auto const                             budget  = std::min(out.size(), natives.size());
  std::size_t const count = Call<QUERY>(context, natives.data(), Narrowed<std::uint32_t>(budget));
  Ensures(count <= budget, "FreeRDP collects within the budget it is given");
  std::ranges::transform(std::span{ natives }.first(count), out.begin(), &WaitHandle::Adopted);
  return out.first(count);
}
}

namespace sdl_rdp::freerdp_facade {
using detail::wait_handle::Forever;
using detail::wait_handle::MaximumWaitHandles;
using detail::wait_handle::WaitHandle;
}
