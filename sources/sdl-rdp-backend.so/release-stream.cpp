#include "_detail/release-stream.hpp"

namespace Backend {
void ReleaseStream::operator()(wStream* stream) const {
  Stream_Free(stream, TRUE);
}
}
