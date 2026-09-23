#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace Backend {
class ErrorStore {
public:
  static char const* Last() { return CallingThread().text->c_str(); }
  static void Publish(ErrorStore* owner, std::string text) {
    auto& caller = CallingThread();
    if (owner) {
      std::scoped_lock const lock(owner->guard);
      auto& slot = owner->errors[std::this_thread::get_id()];
      if (!slot) slot = std::make_shared<std::string>();
      *slot       = std::move(text);
      caller.text = slot;
    } else {
      caller.text = std::make_shared<std::string>(std::move(text));
    }
  }

private:
  struct Cursor {
    std::shared_ptr<std::string> text { std::make_shared<std::string>() };
  };
  static Cursor& CallingThread() {
    // The handle-free C ABI needs a per-thread cursor, also after a failed open or close.
    static thread_local Cursor caller;
    return caller;
  }
  std::mutex                                              guard;
  std::map<std::thread::id, std::shared_ptr<std::string>> errors;
};
}
