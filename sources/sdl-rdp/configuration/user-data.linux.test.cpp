#include <sdl-rdp/configuration/user-data.hpp>

#include <sdl-rdp/utilities/pinned.hpp>

#include <gtest/gtest.h>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

namespace sdl_rdp::configuration::detail::user_data {
namespace {
using sdl_rdp::utilities::Pinned;

// Sets one environment variable for a test's length and puts the previous value back.
class Variable : private Pinned {
public:
  Variable(std::string name, std::string const& value) : _name{ std::move(name) } {
    if (auto const* previous = std::getenv(_name.c_str())) _previous = previous;
    ::setenv(_name.c_str(), value.c_str(), 1);
  }
  ~Variable() {
    if (_previous)
      ::setenv(_name.c_str(), _previous->c_str(), 1);
    else
      ::unsetenv(_name.c_str());
  }

private:
  std::string                _name;
  std::optional<std::string> _previous;
};
}
TEST(UserDataDirectory, IsUnderTheXdgDataHome) {
  Variable const data{ "XDG_DATA_HOME", "/data" };
  EXPECT_EQ(UserDataDirectory(), "/data/sdl-rdp");
}
TEST(UserDataDirectory, WithoutAnXdgDataHomeIsUnderTheHomesLocalShare) {
  Variable const data{ "XDG_DATA_HOME", "" };
  Variable const home{ "HOME", "/home/me"  };
  EXPECT_EQ(UserDataDirectory(), "/home/me/.local/share/sdl-rdp");
}
}
