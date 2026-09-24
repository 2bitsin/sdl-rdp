#include <sdl-rdp/freerdp-facade/release-stream.hpp>

namespace Backend {
auto ReleaseStream::operator()(wStream* stream) const -> void {
  Stream_Free(stream, true);
}
}
