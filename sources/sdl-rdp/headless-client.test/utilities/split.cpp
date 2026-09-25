#include <sdl-rdp/headless-client.test/utilities/split.hpp>

#include <ranges>

namespace sdl_rdp::headless_client_test::utilities::detail::split {
auto Split(std::string_view text, char separator) -> std::vector<std::string_view> {
  std::vector<std::string_view> parts;
  for (auto const part : text | std::views::split(separator)) parts.emplace_back(part);
  return parts;
}
}
