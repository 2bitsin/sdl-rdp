#pragma once
#include <sdl-rdp/utilities/geometry.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace sdl_rdp::settings::detail::aspect {
using sdl_rdp::utilities::AspectRatio;
// A display aspect written N:D, or None (also the default), written empty, for square pixels.
class Aspect {
public:
              Aspect()                                 = default;
              Aspect(std::uint32_t numerator, std::uint32_t denominator);
  static auto None()                           -> Aspect;
  static auto Form()                           -> std::string_view;
  static auto Parsed(std::string_view text)    -> std::optional<Aspect>;
  static auto _Decode(std::string const& text) -> Aspect;
  auto        _Encode() const                  -> std::string;
  auto        Text() const                     -> std::string;
  auto        IsNone() const                   -> bool;
  auto        Ratio() const                    -> std::optional<AspectRatio>;
  auto        operator==(Aspect const&) const  -> bool = default;
private:
  using Parts = std::pair<std::uint32_t, std::uint32_t>;
  std::optional<Parts> _parts;
};
}

namespace sdl_rdp::settings {
using detail::aspect::Aspect;
}
