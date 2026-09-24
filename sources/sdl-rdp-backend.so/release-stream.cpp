#include "_detail/release-stream.hpp"

namespace Backend {
auto ReleaseStream::operator()(wStream* stream) const -> void {
  Stream_Free(stream, TRUE);
}
}
