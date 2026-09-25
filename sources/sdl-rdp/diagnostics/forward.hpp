#pragma once

namespace sdl_rdp::diagnostics::detail::diagnostics {
class Diagnostics;
}
namespace sdl_rdp::diagnostics::detail::failure_log {
class FailureLog;
}
namespace sdl_rdp::diagnostics::detail::trace_queue {
class TraceQueue;
}

namespace sdl_rdp::diagnostics {
using detail::diagnostics::Diagnostics;
using detail::failure_log::FailureLog;
using detail::trace_queue::TraceQueue;
}
