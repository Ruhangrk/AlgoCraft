#include "algocraft/persistence/persistence_service.hpp"

#include <chrono>
#include <exception>
#include <utility>

#include "algocraft/log/log.hpp"

namespace algocraft {

PersistenceService::PersistenceService(SqliteDatabase& write_db, LogHub& logs)
    : db_(&write_db), logs_(&logs) {}

PersistenceService::~PersistenceService() { stop(); }

void PersistenceService::start() {
  if (!stop_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  thread_.start([this] { loop(); });
}

void PersistenceService::stop() {
  if (stop_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  cv_.notify_all();
  thread_.join();
  // Final drain after join so shutdown flush is deterministic.
  if (logs_ != nullptr) {
    logs_->drain_once();
  }
  drain_jobs();
}

void PersistenceService::run_sync(Job job) {
  if (!job) {
    return;
  }
  if (std::this_thread::get_id() == worker_id_) {
    job(db_->handle());
    jobs_done_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  std::promise<void> done;
  auto fut = done.get_future();
  {
    std::lock_guard lock(mu_);
    if (stop_.load(std::memory_order_relaxed)) {
      throw std::runtime_error("PersistenceService: not running");
    }
    if (queue_.size() >= kMaxQueue) {
      jobs_dropped_.fetch_add(1, std::memory_order_relaxed);
      throw std::runtime_error("PersistenceService: write queue full");
    }
    queue_.push(Queued{std::move(job), &done});
  }
  cv_.notify_one();
  fut.get();
}

bool PersistenceService::try_async(Job job) {
  if (!job) {
    return true;
  }
  {
    std::lock_guard lock(mu_);
    if (stop_.load(std::memory_order_relaxed)) {
      return false;
    }
    if (queue_.size() >= kMaxQueue) {
      jobs_dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    queue_.push(Queued{std::move(job), nullptr});
  }
  cv_.notify_one();
  return true;
}

void PersistenceService::loop() {
  worker_id_ = std::this_thread::get_id();
  AC_LOG_INFO("PersistenceService started (T2)");
  while (!stop_.load(std::memory_order_relaxed)) {
    if (logs_ != nullptr) {
      logs_->drain_once();
    }
    drain_jobs();

    std::unique_lock lock(mu_);
    cv_.wait_for(lock, std::chrono::milliseconds(5), [this] {
      return stop_.load(std::memory_order_relaxed) || !queue_.empty();
    });
  }
  AC_LOG_INFO("PersistenceService stopping");
}

void PersistenceService::drain_jobs() {
  for (;;) {
    Queued item;
    {
      std::lock_guard lock(mu_);
      if (queue_.empty()) {
        return;
      }
      item = std::move(queue_.front());
      queue_.pop();
    }

    std::exception_ptr ep;
    try {
      item.job(db_->handle());
      jobs_done_.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
      ep = std::current_exception();
    }

    if (item.done != nullptr) {
      if (ep) {
        item.done->set_exception(ep);
      } else {
        item.done->set_value();
      }
    } else if (ep) {
      try {
        std::rethrow_exception(ep);
      } catch (const std::exception& e) {
        AC_LOG_ERROR("async persist job failed: {}", e.what());
      } catch (...) {
        AC_LOG_ERROR("async persist job failed: unknown");
      }
    }
  }
}

}  // namespace algocraft
