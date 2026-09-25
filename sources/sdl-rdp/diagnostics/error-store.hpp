#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Backend {
class ErrorStore {
public:
  static auto Last()                            -> std::string const&;
  static auto PublishDetached(std::string text) -> void;
  auto        Publish(std::string text)         -> void;

private:
  struct Cursor {
    std::shared_ptr<std::string> text{ std::make_shared<std::string>() };
  };
  static auto CallingThread() -> Cursor&;
  std::mutex                                              guard;
  std::map<std::thread::id, std::shared_ptr<std::string>> errors;
};
}
