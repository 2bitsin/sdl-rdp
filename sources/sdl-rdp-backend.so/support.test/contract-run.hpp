#pragma once
#include <functional>
#include <string>
#include <string_view>

namespace Headless {
class ContractRun {
public:
  explicit ContractRun(std::function<int()> const& body);
  auto     ExpectBroken(std::string_view text, int continuation) const -> void;

private:
  auto ExpectStopped(std::string_view text) const                      -> void;
  auto ExpectComplained(std::string_view text, int continuation) const -> void;
  auto ExpectIgnored(int continuation) const                           -> void;
  auto Aborted() const                                                 -> bool;
  auto Exited(int code) const                                          -> bool;
  int         _status{ };
  std::string _errors;
};
}
