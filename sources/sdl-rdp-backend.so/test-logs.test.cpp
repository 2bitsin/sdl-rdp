#include "_detail/test-logs.hpp"

namespace Headless {
void Logs::Collect(void* user, sdlrdp_log_level level, char const* text) {
  utilities::Expects(user != nullptr, "log sink exists");
  utilities::Expects(text != nullptr, "log line exists");
  auto&                  self = *static_cast<Logs*>(user);
  std::scoped_lock const lock(self.guard);
  self.lines.emplace_back(level, text);
  self.changed.notify_all();
}
std::string Logs::Text(bool include_info) {
  std::scoped_lock const lock(guard);
  return lines | std::views::filter([=](auto const& line) { return include_info || line.first != SDLRDP_LOG_INFO; }) |
         std::views::transform([](auto const& line) {
           auto level = line.first == SDLRDP_LOG_ERROR ? "ERROR" : line.first == SDLRDP_LOG_WARN ? "WARN" : "INFO";
           return std::string(level) + ": " + line.second;
         }) |
         std::views::join_with('\n') | std::ranges::to<std::string>();
}
unsigned Logs::Count(sdlrdp_log_level level, std::string_view text) {
  return Matching([=](auto const& line) { return line.first == level && line.second.contains(text); });
}
bool Logs::Contains(sdlrdp_log_level level, std::string_view text) {
  return Count(level, text) != 0;
}
bool Logs::Contains(std::string_view text) {
  return Matching([=](auto const& line) { return line.second.contains(text); }) != 0;
}
std::vector<std::pair<sdlrdp_log_level, std::string>> Logs::Entries() {
  std::scoped_lock const lock(guard);
  return lines;
}
}
