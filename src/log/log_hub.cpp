#include "algocraft/log/log_hub.hpp"

#include <thread>

#include <chrono>
#include <string_view>

#include <spdlog/spdlog.h>

#include "algocraft/domain/timestamp.hpp"

namespace algocraft {
namespace {

spdlog::level::level_enum to_spdlog(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return spdlog::level::trace;
    case LogLevel::Debug:
      return spdlog::level::debug;
    case LogLevel::Info:
      return spdlog::level::info;
    case LogLevel::Warn:
      return spdlog::level::warn;
    case LogLevel::Error:
      return spdlog::level::err;
  }
  return spdlog::level::info;
}

}  // namespace

LogHub::LogHub() = default;

LogHub::~LogHub() { stop(); }

void LogHub::start() {
  if (!stop_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  drain_.start([this] { drain_loop(); });
}

void LogHub::stop() {
  if (stop_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  drain_.join();
  drain_available();
  const auto drops = dropped_.load(std::memory_order_relaxed);
  if (drops > 0) {
    spdlog::warn("log_hub dropped={} written={}", drops,
                 written_.load(std::memory_order_relaxed));
  }
}

bool LogHub::try_enqueue(const LogEvent& event) {
  LogEvent copy = event;
  if (copy.timestamp_ns == 0) {
    copy.timestamp_ns = Timestamp::now().nanos();
  }

  {
    std::lock_guard lock(mu_);
    if (size_ >= kCapacity) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    slots_[head_] = copy;
    head_ = (head_ + 1) % kCapacity;
    ++size_;
  }
  return true;
}

void LogHub::drain_once() {
  const auto before = written_.load(std::memory_order_relaxed);
  drain_available();
  const auto drops = dropped_.load(std::memory_order_relaxed);
  static thread_local std::uint64_t last_drop_report = 0;
  if (drops > last_drop_report) {
    spdlog::warn("log_hub queue full; dropped_total={}", drops);
    last_drop_report = drops;
  }
  (void)before;
}

void LogHub::drain_loop() {
  std::uint64_t last_drop_report = 0;
  while (!stop_.load(std::memory_order_relaxed)) {
    const auto before = written_.load(std::memory_order_relaxed);
    drain_available();
    const auto drops = dropped_.load(std::memory_order_relaxed);
    if (drops > last_drop_report) {
      spdlog::warn("log_hub queue full; dropped_total={}", drops);
      last_drop_report = drops;
    }
    if (written_.load(std::memory_order_relaxed) == before) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }
  drain_available();
}

void LogHub::drain_available() {
  for (;;) {
    LogEvent event{};
    {
      std::lock_guard lock(mu_);
      if (size_ == 0) {
        break;
      }
      event = slots_[tail_];
      tail_ = (tail_ + 1) % kCapacity;
      --size_;
    }
    emit(event);
    written_.fetch_add(1, std::memory_order_relaxed);
  }
}

void LogHub::emit(const LogEvent& event) {
  const std::string_view text(event.message.data(), event.length);
  spdlog::log(to_spdlog(event.level), "{}", text);
}

}  // namespace algocraft
