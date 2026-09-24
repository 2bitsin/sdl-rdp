#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Backend {
class ErrorStore {
public:
  static char const* Last();
  static void        Publish(ErrorStore* owner, std::string text);

private:
  struct Cursor {
    std::shared_ptr<std::string> text{ std::make_shared<std::string>() };
  };
  static Cursor& CallingThread();
  std::mutex                                              guard;
  std::map<std::thread::id, std::shared_ptr<std::string>> errors;
};
}
