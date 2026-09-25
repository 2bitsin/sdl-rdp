#pragma once
#include <sdl-rdp/configuration/refresh-mode.hpp>
#include <sdl-rdp/utilities/bounded.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>

namespace sdl_rdp::settings::detail::refresh {
using sdl_rdp::configuration::RefreshMode;
using sdl_rdp::utilities::Bounded;

// A display refresh: a fixed rate, or a mode that follows the client or the sender from the initial rate.
class Refresh {
public:
  // SDL carries a display's rate in millihertz as int.
  using Rate = Bounded<std::uint32_t, 1, std::numeric_limits<std::int32_t>::max() / std::milli::den>;
              Refresh()                                = default;
  explicit    Refresh(Rate rate);
  explicit    Refresh(RefreshMode mode);
  static auto Form()                           -> std::string_view;
  static auto Parsed(std::string_view text)    -> std::optional<Refresh>;
  static auto _Decode(std::string const& text) -> Refresh;
  auto        _Encode() const                  -> std::string;
  auto        Mode() const                     -> RefreshMode;
  auto        Hz() const                       -> std::uint32_t;
  auto        operator==(Refresh const&) const -> bool = default;
private:
  static constexpr std::uint32_t InitialHz = 60;
  RefreshMode                    _mode     { RefreshMode::Fixed };
  Rate                           _rate     { InitialHz          };
};
}

namespace sdl_rdp::settings {
using detail::refresh::Refresh;
}
