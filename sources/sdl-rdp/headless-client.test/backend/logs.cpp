#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <cstddef>
#include <regex>

namespace sdl_rdp::headless_client_test::backend::detail::logs {
using sdl_rdp::diagnostics::LogLevel;
auto Logs::Log(LogLevel level, std::string_view text) -> void {
  std::scoped_lock const lock(guard);
  lines.emplace_back(level, text);
  changed.notify_all();
}
auto Logs::Text(bool include_info) -> std::string {
  std::scoped_lock const lock(guard);
  return lines | std::views::filter([=](auto const& line) { return include_info || line.first != LogLevel::Info; })
         | std::views::transform([](auto const& line) {
             auto level = line.first == LogLevel::Error ? "ERROR" : line.first == LogLevel::Warn ? "WARN" : "INFO";
             return std::string(level) + ": " + line.second;
           })
         | std::views::join_with('\n') | std::ranges::to<std::string>();
}
auto Logs::Count(LogLevel level, std::string_view text) -> std::size_t {
  return Matching([=](auto const& line) { return line.first == level && line.second.contains(text); });
}
auto Logs::Contains(LogLevel level, std::string_view text) -> bool {
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
  return std::views::iota(std::size_t{ 0 }, match.size())
         | std::views::transform([&match](std::size_t group) { return match.str(group); })
         | std::ranges::to<std::vector>();
}
auto Logs::Entries() -> std::vector<std::pair<LogLevel, std::string>> {
  std::scoped_lock const lock(guard);
  return lines;
}
}
