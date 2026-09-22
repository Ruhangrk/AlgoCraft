#include "algocraft/backtest/backtest_service.hpp"

#include <stdexcept>
#include <string>

#include "algocraft/backtest/backtest_runner.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/instrument.hpp"
#include "algocraft/market_data/cached_provider.hpp"
#include "algocraft/market_data/upstox_provider.hpp"

namespace algocraft {
namespace {

void bind_upstox_key(DataSourceRegistry& data, std::string_view ticker,
                     std::string_view instrument_key) {
  if (instrument_key.empty()) {
    return;
  }
  auto* cached = dynamic_cast<CachedProvider*>(&data.active_provider());
  if (cached == nullptr) {
    return;
  }
  auto* upstox = dynamic_cast<UpstoxProvider*>(&cached->vendor());
  if (upstox == nullptr) {
    return;
  }
  upstox->upstox_loader().set_instrument_key(ticker, std::string{instrument_key});
}

std::int64_t return_pct_bp(std::int64_t capital_paise, std::int64_t pnl_paise) {
  if (capital_paise <= 0) {
    return 0;
  }
  return (pnl_paise * 10000) / capital_paise;
}

}  // namespace

BacktestService::BacktestService(WorkbookRepository& workbooks, BacktestRepository& backtests,
                                 DataSourceRegistry& data, DataFetchService& fetch,
                                 StrategyRegistry& strategies, SymbolTable& symbols,
                                 InstrumentRepository* instruments)
    : workbooks_(&workbooks),
      backtests_(&backtests),
      data_(&data),
      fetch_(&fetch),
      strategies_(&strategies),
      symbols_(&symbols),
      instruments_(instruments) {}

ManualBacktestOutcome BacktestService::run(const ManualBacktestRequest& request) {
  if (request.ticker.empty() || request.strategy_name.empty()) {
    throw std::invalid_argument("ticker and strategy_name required");
  }
  if (request.from.nanos() == 0 || request.to.nanos() == 0 || request.from > request.to) {
    throw std::invalid_argument("invalid from/to range");
  }
  if (request.capital.paise() <= 0) {
    throw std::invalid_argument("capital must be positive");
  }

  // Workbook is ownership/scope only — manual backtest capital is simulated, not borrowed.
  if (!workbooks_->find(request.workbook_id)) {
    throw std::runtime_error("workbook not found");
  }

  if (instruments_ != nullptr) {
    const auto inst = instruments_->find_by_ticker(request.ticker);
    if (inst && !inst->instrument_key.empty()) {
      bind_upstox_key(*data_, request.ticker, inst->instrument_key);
    }
  }

  symbols_->intern({.ticker = request.ticker}, {});
  const auto symbol_id = symbols_->find(request.ticker);
  if (!symbol_id) {
    throw std::runtime_error("symbol intern failed");
  }

  fetch_->ensure_data_available(request.ticker, request.from, request.to, BarResolution::OneMin);

  BacktestRequest req{};
  req.symbol_id = *symbol_id;
  req.from = request.from;
  req.to = request.to;
  req.starting_capital = request.capital;
  req.strategy_name = request.strategy_name;
  req.strategy = request.strategy;
  req.strategy.symbol_id = *symbol_id;

  BacktestRunner runner;
  ManualBacktestOutcome out{};
  out.result = runner.run(*data_, *strategies_, req);

  BacktestRow row{};
  row.workbook_id = request.workbook_id;
  row.strategy_name = request.strategy_name;
  row.ticker = request.ticker;
  row.capital_paise = request.capital.paise();
  row.from_ns = request.from.nanos();
  row.to_ns = request.to.nanos();
  row.ending_equity_paise = out.result.ending_equity_paise;
  row.pnl_paise = out.result.realized_pnl_paise;
  row.fees_paise = out.result.fees_paise;
  row.return_pct_bp = return_pct_bp(row.capital_paise, row.pnl_paise);
  row.max_drawdown_paise = out.result.max_drawdown_paise;
  row.fills = out.result.fills;
  row.bars = static_cast<int>(out.result.bars);
  row.status = "completed";
  row.id = backtests_->insert(row);
  out.row = *backtests_->find(request.workbook_id, row.id);
  return out;
}

}  // namespace algocraft
