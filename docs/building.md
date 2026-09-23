# Building

Linux only for now. The tree needs a C++23 compiler (GCC 14 or newer), CMake,
ninja, Python 3 and conan 2; FreeRDP 3 with its development headers
(`freerdp3-dev` and `libwinpr3-dev` on Debian and Ubuntu) and OpenSSL from the
system; `nv-codec-headers` and gtest from conan. AVC420 needs an NVIDIA driver
with NVENC at runtime and is skipped without one. The build driver `buildutil`
and the `oxbox` utility library the backend uses are not published yet; until
they are, the tree builds only where a conan remote and a package index provide
them. `./buildutil build` builds everything, `./buildutil test` runs the gate,
and `./buildutil build --release` writes the release libraries under
`_build/<profile>/`.

# Consuming

`buildutil publish` packages `sdl-rdp` for conan. A project takes it with one
`Require` line and links the `SDL3` component; the backend is never linked,
only found next to it at runtime.
