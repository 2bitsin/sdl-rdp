#pragma once
#include "options.hpp"
#include <sdl-rdp/SDL3/rdp/backend/sdl-internals.hpp>
#include <optional>
#include <string>
namespace sdl3::rdp::settings::detail::configured_text {
// The text settings the backend opens with; the ABI record points into the strings held here.
class ConfiguredText {
public:
  explicit ConfiguredText(Options const& options);
  auto     Fill(sdlrdp_config& config) const -> void;
private:
  std::optional<std::string> _bind;
  std::optional<std::string> _cert_dir;
  std::optional<std::string> _user;
  std::optional<std::string> _password;
  std::optional<std::string> _domain;
};
}

namespace sdl3::rdp::settings {
using detail::configured_text::ConfiguredText;
}
