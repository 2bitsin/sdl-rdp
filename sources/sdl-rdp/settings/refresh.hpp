#pragma once
#include <sdl-rdp/utilities/bounded.hpp>

#include <_buildutil/reflect.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>

namespace sdl_rdp::settings::detail::refresh {
// Values are the frozen backend set_refresh mode argument; the labels are the automatic modes' written names.
enum class RefreshMode : std::uint32_t {
  FIXED = 0,
  CLIENT _Label("auto-client")                 = 1,
  CLIENT_AVERAGE _Label("auto-client-average") = 2,
  SENDER _Label("auto-sender")                 = 3
};
constexpr auto reflect_scheme(RefreshMode* tag);
// A display refresh: a fixed rate, or a mode that follows the client or the sender from the initial rate.
class Refresh {
public:
  // SDL carries a display's rate in millihertz as int.
  using Rate = utilities::Bounded<std::uint32_t, 1, std::numeric_limits<std::int32_t>::max() / std::milli::den>;
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
  RefreshMode                    _mode     { RefreshMode::FIXED };
  Rate                           _rate     { InitialHz          };
};
}
namespace sdl_rdp::settings {
using detail::refresh::RefreshMode;
using detail::refresh::Refresh;
}
