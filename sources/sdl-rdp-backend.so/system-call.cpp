#include "_detail/system-call.hpp"

#include "_detail/contract.hpp"

#include <cerrno>
#include <string>
#include <system_error>

namespace Backend {
auto SystemCall(int result, std::string_view operation) -> int {
  utilities::Expects(!operation.empty(), "the failing operation can be named");
  if (result < 0) throw std::system_error(errno, std::system_category(), std::string(operation));
  return result;
}
}
