#include <sdl-rdp/auth/certificate.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>

namespace sdl_rdp::auth::detail::certificate {
namespace {
using oxbox::platform::ScratchArea;
}
// SDL_RDP_CERT_DIR=/x/certs/ reaches Credentials as written.
TEST(EnsureCertificate, GeneratesIntoADirectoryWrittenWithATrailingSlash) {
  ScratchArea const area       { "certificate", "sdl-rdp"   };
  Credentials const credentials{ area.Path() / "certs" / "" };
  EnsureCertificate(credentials);
  EXPECT_TRUE(credentials.Exist());
}
}
