#pragma once
#include <cstdint>
#include <sys/types.h>

namespace sdl_rdp::sample_gate_test::process::detail::procfs {
auto ProcfsSelf()                 -> pid_t;
auto ListeningPort(pid_t pid = 0) -> std::uint32_t;
}

namespace sdl_rdp::sample_gate_test::process {
using detail::procfs::ListeningPort;
using detail::procfs::ProcfsSelf;
}
