#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>

#include "algocraft/engine/engine_thread.hpp"
#include "algocraft/log/log_hub.hpp"
#include "algocraft/persistence/sqlite_database.hpp"

struct sqlite3;

namespace algocraft {

// Thread 2 — sole owner of db_write. Also drains LogHub (no separate log_drain).
// Producers enqueue work via run_sync / try_async; never touch write handle themselves.
class PersistenceService {
public:
  using Job = std::function<void(sqlite3*)>;

  PersistenceService(SqliteDatabase& write_db, LogHub& logs);
  ~PersistenceService();

  PersistenceService(const PersistenceService&) = delete;
  PersistenceService& operator=(const PersistenceService&) = delete;

  void start();
  void stop();

  // Blocks until T2 finishes job (or rethrows). Safe from API / live / CLI threads.
  // If already on T2, runs inline (no deadlock).
  void run_sync(Job job);

  // Never blocks. false = queue full (dropped).
  [[nodiscard]] bool try_async(Job job);

  [[nodiscard]] bool running() const { return !stop_.load(std::memory_order_relaxed); }
  [[nodiscard]] std::uint64_t jobs_done() const {
    return jobs_done_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t jobs_dropped() const {
    return jobs_dropped_.load(std::memory_order_relaxed);
  }

  static constexpr std::size_t kMaxQueue = 4096;

private:
  struct Queued {
    Job job;
    std::promise<void>* done{nullptr};  // null = async
  };

  void loop();
  void drain_jobs();

  SqliteDatabase* db_{nullptr};
  LogHub* logs_{nullptr};
  EngineThread thread_{"thread2_persistence"};

  std::mutex mu_{};
  std::condition_variable cv_{};
  std::queue<Queued> queue_{};
  std::atomic<bool> stop_{true};
  std::thread::id worker_id_{};

  std::atomic<std::uint64_t> jobs_done_{0};
  std::atomic<std::uint64_t> jobs_dropped_{0};
};

}  // namespace algocraft
