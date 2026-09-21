#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "algocraft/auth/auth_service.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/market_data/data_fetch_service.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/persistence/activity_repository.hpp"
#include "algocraft/persistence/backtest_repository.hpp"
#include "algocraft/persistence/instrument_repository.hpp"
#include "algocraft/persistence/sqlite_database.hpp"
#include "algocraft/persistence/workbook_repository.hpp"
#include "algocraft/strategies/strategy_registry.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

namespace algocraft {

// Thread-4 HTTP + WebSocket API (Crow). Never called from hot path.
class HttpServer {
public:
  struct Config {
    std::string host{"127.0.0.1"};
    int port{8080};
    std::string jwt_secret{"algocraft-dev-secret-change-me"};
  };

  HttpServer(Config config, SqliteDatabase& db, DataSourceRegistry& data,
             StrategyRegistry& strategies, SymbolTable& symbols,
             DataFetchService* fetch = nullptr);
  ~HttpServer();

  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  void start();  // blocking listen
  void start_async();
  void stop();

  [[nodiscard]] int port() const { return config_.port; }

private:
  Config config_{};
  SqliteDatabase& db_;
  DataSourceRegistry& data_;
  StrategyRegistry& strategies_;
  SymbolTable& symbols_;
  DataFetchService* fetch_{nullptr};
  AuthService auth_;
  ActivityRepository activity_;
  WorkbookRepository workbooks_;
  InstrumentRepository instruments_;
  BacktestRepository backtests_;
  WorkbookManager books_{};
  std::atomic<bool> running_{false};
  std::unique_ptr<std::thread> thread_{};
  void* app_{nullptr};  // crow::SimpleApp* while running
};

}  // namespace algocraft
