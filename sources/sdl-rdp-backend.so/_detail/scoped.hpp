#pragma once
#include "contract.hpp"
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace utilities {
template <typename _Value, auto _IsNull, auto _MakeNull>
concept NullableResource = std::predicate<decltype(_IsNull), _Value const&>
                           && std::invocable<decltype(_MakeNull), _Value&>;
template <typename _VTy, auto _Ctor, auto _Dtor, auto _IsNull = nullptr, auto _MakeNull = nullptr>
class RAIIWrap {
public:
  template <typename... _Args>
    requires std::invocable<decltype(_Ctor), _Args...>
  explicit RAIIWrap(_Args&&... args) : _value{ std::invoke(_Ctor, std::forward<_Args>(args)...) } {
    if constexpr (_Checked) Ensures(_Live(), "acquisition produced a live resource");
  }
  RAIIWrap(RAIIWrap const&) = delete;
  RAIIWrap(RAIIWrap&& other) noexcept
    requires NullableResource<_VTy, _IsNull, _MakeNull>
      : _value{ other.Release() } { }
  ~RAIIWrap() noexcept {
    _Free();
  }
  auto operator=(RAIIWrap const&) -> RAIIWrap& = delete;
  auto operator=(RAIIWrap&& other) noexcept -> RAIIWrap&
    requires NullableResource<_VTy, _IsNull, _MakeNull>
  {
    if (this != &other) {
      _Free();
      _value = other.Release();
    }
    return *this;
  }
  explicit operator bool() const noexcept {
    return _Live();
  }
  auto Get() const noexcept -> decltype(auto) {
    if constexpr (std::is_reference_v<_VTy>)
      return _value.get();
    else
      return std::as_const(_value);
  }
  auto Release() noexcept -> _VTy
    requires NullableResource<_VTy, _IsNull, _MakeNull>
  {
    auto value = std::move(_value);
    std::invoke(_MakeNull, _value);
    return value;
  }
  auto Close() noexcept -> std::invoke_result_t<decltype(_Dtor), _VTy>
    requires NullableResource<_VTy, _IsNull, _MakeNull>
  {
    Expects(_Live(), "close requires a live resource");
    return std::invoke(_Dtor, Release());
  }
private:
  using _Stored = std::conditional_t<std::is_reference_v<_VTy>, std::reference_wrapper<std::remove_reference_t<_VTy>>,
                                     _VTy>;
  auto _Live() const noexcept -> bool {
    if constexpr (_Checked)
      return !std::invoke(_IsNull, _value);
    else
      return true;
  }
  auto _Free() noexcept -> void {
    if (_Live()) std::invoke(_Dtor, _value);
  }
  static constexpr bool _Checked = NullableResource<_VTy, _IsNull, _MakeNull>;
  _Stored _value;
};
}
