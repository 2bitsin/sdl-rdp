#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <cstddef>
#include <regex>

namespace sdl_rdp::headless_client_test::backend::detail::logs {
auto Logs::Collect(void* user, sdlrdp_log_level level, char const* text) -> void {
  Expects(user != nullptr, "log sink exists");
  Expects(text != nullptr, "log line exists");
  auto&                  self = *static_cast<Logs*>(user);
  std::scoped_lock const lock(self.guard);
  self.lines.emplace_back(level, text);
  self.changed.notify_all();
}
auto Logs::Text(bool include_info) -> std::string {
  std::scoped_lock const lock(guard);
  return lines | std::views::filter([=](auto const& line) { return include_info || line.first != SDLRDP_LOG_INFO; })
         | std::views::transform([](auto const& line) {
             auto level = line.first == SDLRDP_LOG_ERROR ? "ERROR" : line.first == SDLRDP_LOG_WARN ? "WARN" : "INFO";
             return std::string(level) + ": " + line.second;
           })
         | std::views::join_with('\n') | std::ranges::to<std::string>();
}
auto Logs::Count(sdlrdp_log_level level, std::string_view text) -> std::size_t {
  return Matching([=](auto const& line) { return line.first == level && line.second.contains(text); });
}
auto Logs::Contains(sdlrdp_log_level level, std::string_view text) -> bool {
  return Count(level, text) != 0;
}
auto Logs::Contains(std::string_view text) -> bool {
  return Matching([=](auto const& line) { return line.second.contains(text); }) != 0;
}
// The whole match is group 0; nullopt when the collected text holds no match.
auto Logs::Statistics(std::string_view pattern) -> std::optional<std::vector<std::string>> {
  auto const  text  = Text(true);
  std::smatch match;
  if (!std::regex_search(text, match, std::regex(pattern.begin(), pattern.end()))) return std::nullopt;
  return match | std::views::transform([](auto const& group) { return group.str(); }) | std::ranges::to<std::vector>();
}
auto Logs::Entries() -> std::vector<std::pair<sdlrdp_log_level, std::string>> {
  std::scoped_lock const lock(guard);
  return lines;
}
}
