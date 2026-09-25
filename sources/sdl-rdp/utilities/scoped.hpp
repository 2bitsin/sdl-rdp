#pragma once
#include <sdl-rdp/utilities/contract.hpp>
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace utilities {
template <typename ValueTy, auto IS_NULL, auto MAKE_NULL>
concept NullableResource = std::predicate<decltype(IS_NULL), ValueTy const&>
                           && std::invocable<decltype(MAKE_NULL), ValueTy&>;
template <typename VTy, auto CTOR, auto DTOR, auto IS_NULL = nullptr, auto MAKE_NULL = nullptr>
class RAIIWrap {
public:
  template <typename... ArgsTy>
    requires std::invocable<decltype(CTOR), ArgsTy...>
  explicit RAIIWrap(ArgsTy&&... args) : _value{ std::invoke(CTOR, std::forward<ArgsTy>(args)...) } {
    if constexpr (Checked) Ensures(Live(), "acquisition produced a live resource");
  }
  RAIIWrap(RAIIWrap const&)  = delete;
  RAIIWrap(RAIIWrap&& other) noexcept
    requires NullableResource<VTy, IS_NULL, MAKE_NULL>
      : _value{ other.Release() } { }
  ~RAIIWrap()                noexcept {
    Free();
  }
  auto     operator=(RAIIWrap const&)           -> RAIIWrap& = delete;
  auto     operator=(RAIIWrap&& other) noexcept -> RAIIWrap&
    requires NullableResource<VTy, IS_NULL, MAKE_NULL>
  {
    if (this != &other) {
      Free();
      _value = other.Release();
    }
    return *this;
  }
  explicit operator bool() const                             noexcept {
    return Live();
  }
  auto Get() const noexcept -> decltype(auto) {
    if constexpr (std::is_reference_v<VTy>)
      return _value.get();
    else
      return std::as_const(_value);
  }
  auto Release() noexcept -> VTy
    requires NullableResource<VTy, IS_NULL, MAKE_NULL>
  {
    auto value = std::move(_value);
    std::invoke(MAKE_NULL, _value);
    return value;
  }
  auto Close() noexcept   -> std::invoke_result_t<decltype(DTOR), VTy>
    requires NullableResource<VTy, IS_NULL, MAKE_NULL>
  {
    Expects(Live(), "close requires a live resource");
    return std::invoke(DTOR, Release());
  }
private:
  using Stored = std::conditional_t<std::is_reference_v<VTy>, std::reference_wrapper<std::remove_reference_t<VTy>>,
                                    VTy>;
  auto Live() const noexcept -> bool {
    if constexpr (Checked)
      return !std::invoke(IS_NULL, _value);
    else
      return true;
  }
  auto Free() noexcept -> void {
    if (Live()) std::invoke(DTOR, _value);
  }
  static constexpr bool Checked = NullableResource<VTy, IS_NULL, MAKE_NULL>;
  Stored                _value;
};
}
