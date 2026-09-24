# present_cost

`present_cost.py` is run by hand, never by the gate. It measures three deterministic per-present numbers on the release
sample under callgrind, with xfreerdp3 attached under Xvfb at 1280x800:

- the `memset` instructions `rdp::Driver::Poll` spends on every `SDL_PumpEvents` (0 since #37);
- the compose instructions, self cost of `Backend::Presenter::Present` plus `Backend::FrameSnapshot::*`;
- the heap allocations made by the driver's own functions.

Build with `./buildutil build --release --no-tests --option contracts=ignore`, then run
`python3 tools/perf/present_cost.py run --out <dir>`. It needs valgrind, Xvfb, xfreerdp3 and ss. Each scenario (default
planar partial, planar full, progressive full) is instrumented for `--seconds` after the client focuses the window.
`present_cost.py analyze <dir>/*.callgrind` re-reads saved profiles, so builds before and after a change compare
file to file.
