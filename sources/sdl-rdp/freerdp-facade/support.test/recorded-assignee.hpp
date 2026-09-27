#pragma once
#include <sdl-rdp/freerdp-facade/assignment-sink.hpp>
#include <sdl-rdp/freerdp-facade/support.test/recorded-failures.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sdl_rdp::freerdp_facade::support_test::detail::recorded_assignee {
using sdl_rdp::freerdp_facade::AssignmentSink;
using sdl_rdp::freerdp_facade::support_test::RecordedFailures;

// A test's channel-id slot owner: the id it was given, or a throw once it refuses.
class RecordedAssignee final : public RecordedFailures<AssignmentSink> {
public:
  explicit RecordedAssignee(std::vector<std::string>& failures) noexcept;
  auto     ChannelAssigned(std::uint32_t id) -> void            override;
  auto     Refuse() noexcept                 -> void;
  auto     Assigned() const noexcept         -> std::optional<std::uint32_t>;

private:
  bool                         _refuses { };
  std::optional<std::uint32_t> _assigned;
};
}

namespace sdl_rdp::freerdp_facade::support_test {
using detail::recorded_assignee::RecordedAssignee;
}
