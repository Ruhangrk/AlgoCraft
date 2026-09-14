#include "algocraft/engine/phase0_runtime.hpp"

#include <string_view>
#include <thread>

#include <spdlog/spdlog.h>

namespace algocraft {
namespace {

void drain_event_ring(Phase0Runtime::EventRing& ring, std::atomic<std::uint64_t>& counter) {
  DummyEvent event{};
  while (ring.try_pop(event)) {
    counter.fetch_add(1, std::memory_order_relaxed);
  }
}

}  // namespace

Phase0Runtime::Phase0Runtime() = default;

Phase0Runtime::~Phase0Runtime() { stop(); }

void Phase0Runtime::start() {
  if (!stop_workers_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  stop_persist_.store(false, std::memory_order_release);
  thread0_.start([this] { hot_path_loop(); });
  thread1_.start([this] { routing_loop(); });
  thread2_.start([this] { persistence_loop(); });
  thread3_.start([this] { execution_loop(); });
  thread4_.start([this] { api_loop(); });
}

void Phase0Runtime::stop() {
  if (stop_workers_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  thread0_.join();
  thread1_.join();
  thread3_.join();
  thread4_.join();
  stop_persist_.store(true, std::memory_order_release);
  thread2_.join();
}

void Phase0Runtime::hot_path_loop() {
  DummyEvent event{};
  while (!stop_workers_.load(std::memory_order_relaxed)) {
    bool work = false;
    if (market_data_.try_pop(event)) {
      work = true;
      (void)routing_.try_push(event);
      DummyEvent persist = event;
      persist.kind = kEventPersist;
      (void)persist0_.try_push(persist);
      (void)order_out_.try_push(event);
      (void)logger_.try_log(LogLevel::Info, "bar");
      bars_processed_.fetch_add(1, std::memory_order_relaxed);
    }
    if (fill_in_.try_pop(event)) {
      work = true;
      DummyEvent persist = event;
      persist.kind = kEventPersist;
      (void)persist0_.try_push(persist);
      fills_processed_.fetch_add(1, std::memory_order_relaxed);
    }
    if (command_.try_pop(event)) {
      work = true;
      commands_processed_.fetch_add(1, std::memory_order_relaxed);
    }
    if (!work) {
      std::this_thread::yield();
    }
  }
}

void Phase0Runtime::routing_loop() {
  DummyEvent event{};
  while (!stop_workers_.load(std::memory_order_relaxed)) {
    if (routing_.try_pop(event)) {
      DummyEvent cmd{};
      cmd.kind = kEventCommand;
      cmd.symbol_id = event.symbol_id;
      cmd.seq = event.seq;
      (void)command_.try_push(cmd);
      DummyEvent persist = event;
      persist.kind = kEventPersist;
      (void)persist1_.try_push(persist);
    } else {
      std::this_thread::yield();
    }
  }
}

void Phase0Runtime::persistence_loop() {
  DummyEvent event{};
  LogEvent log{};
  while (!stop_persist_.load(std::memory_order_relaxed)) {
    bool work = false;
    if (persist0_.try_pop(event)) {
      work = true;
      persist_events_.fetch_add(1, std::memory_order_relaxed);
    }
    if (persist1_.try_pop(event)) {
      work = true;
      persist_events_.fetch_add(1, std::memory_order_relaxed);
    }
    if (persist3_.try_pop(event)) {
      work = true;
      persist_events_.fetch_add(1, std::memory_order_relaxed);
    }
    if (log_ring_.try_pop(log)) {
      work = true;
      const std::string_view text(log.message.data(), log.length);
      spdlog::info("[persist] {}", text);
      logs_written_.fetch_add(1, std::memory_order_relaxed);
    }
    if (!work) {
      std::this_thread::yield();
    }
  }
  drain_event_ring(persist0_, persist_events_);
  drain_event_ring(persist1_, persist_events_);
  drain_event_ring(persist3_, persist_events_);
  while (log_ring_.try_pop(log)) {
    logs_written_.fetch_add(1, std::memory_order_relaxed);
  }
}

void Phase0Runtime::execution_loop() {
  DummyEvent event{};
  while (!stop_workers_.load(std::memory_order_relaxed)) {
    if (order_out_.try_pop(event)) {
      DummyEvent fill = event;
      fill.kind = kEventFill;
      (void)fill_in_.try_push(fill);
      DummyEvent persist = event;
      persist.kind = kEventPersist;
      (void)persist3_.try_push(persist);
    } else {
      std::this_thread::yield();
    }
  }
}

void Phase0Runtime::api_loop() {
  while (!stop_workers_.load(std::memory_order_relaxed)) {
    std::this_thread::yield();
  }
}

}  // namespace algocraft
