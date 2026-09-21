#include "algocraft/api/http_server.hpp"

#include <spdlog/spdlog.h>

#include "algocraft/api/auth_routes.hpp"
#include "algocraft/api/http_helpers.hpp"
#include "algocraft/api/market_routes.hpp"
#include "algocraft/api/workbook_routes.hpp"

namespace algocraft {

HttpServer::HttpServer(Config config, SqliteDatabase& db, DataSourceRegistry& data,
                       StrategyRegistry& strategies, SymbolTable& symbols, DataFetchService* fetch)
    : config_{std::move(config)},
      db_{db},
      data_{data},
      strategies_{strategies},
      symbols_{symbols},
      fetch_{fetch},
      auth_{db.handle(), config_.jwt_secret},
      activity_{db.handle()},
      workbooks_{db.handle()} {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::stop() {
  running_ = false;
  if (app_ != nullptr) {
    static_cast<api::App*>(app_)->stop();
  }
  if (thread_ && thread_->joinable()) {
    thread_->join();
  }
  thread_.reset();
}

void HttpServer::start_async() {
  thread_ = std::make_unique<std::thread>([this] { start(); });
}

void HttpServer::start() {
  api::App app;
  app_ = &app;
  running_ = true;

  api::register_auth_routes(app, auth_);
  api::register_market_routes(app, api::MarketRouteDeps{
                                       .auth = auth_,
                                       .strategies = strategies_,
                                       .data = data_,
                                       .symbols = symbols_,
                                       .fetch = fetch_,
                                   });
  api::register_workbook_routes(app, api::WorkbookRouteDeps{
                                         .auth = auth_,
                                         .workbooks = workbooks_,
                                         .activity = activity_,
                                         .books = books_,
                                         .data = data_,
                                         .strategies = strategies_,
                                         .symbols = symbols_,
                                     });

  app.loglevel(crow::LogLevel::Warning);
  spdlog::info("API listening on http://{}:{} (Crow)", config_.host, config_.port);
  app.bindaddr(config_.host).port(static_cast<std::uint16_t>(config_.port)).run();
  app_ = nullptr;
  running_ = false;
}

}  // namespace algocraft
