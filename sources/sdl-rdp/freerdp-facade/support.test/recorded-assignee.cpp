#include <sdl-rdp/freerdp-facade/support.test/recorded-assignee.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace sdl_rdp::freerdp_facade::support_test::detail::recorded_assignee {
RecordedAssignee::RecordedAssignee(std::vector<std::string>& failures) noexcept : RecordedFailures{ failures } { }
auto RecordedAssignee::ChannelAssigned(std::uint32_t id) -> void {
  if (_refuses) throw std::runtime_error{ "refused" };
  _assigned = id;
}
auto RecordedAssignee::Refuse() noexcept -> void {
  _refuses = true;
}
auto RecordedAssignee::Assigned() const noexcept -> std::optional<std::uint32_t> {
  return _assigned;
}
}
