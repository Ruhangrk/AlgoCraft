#pragma once

#include <functional>
#include <string>
#include <thread>
#include <utility>

namespace algocraft {

// Named std::thread. Destructor joins so a thread cannot be left running.
class EngineThread {
public:
  explicit EngineThread(std::string name) : name_(std::move(name)) {}

  EngineThread(const EngineThread&) = delete;
  EngineThread& operator=(const EngineThread&) = delete;
  EngineThread(EngineThread&&) = delete;
  EngineThread& operator=(EngineThread&&) = delete;

  ~EngineThread() { join(); }

  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] bool joinable() const { return thread_.joinable(); }

  void start(std::function<void()> work) {
    if (thread_.joinable()) {
      return;
    }
    thread_ = std::thread(std::move(work));
  }

  void join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

private:
  std::string name_;
  std::thread thread_{};
};

}  // namespace algocraft
