#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "algocraft/container/container_manager.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/routing/routing_algo.hpp"
#include "algocraft/strategies/strategy_registry.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

namespace algocraft {

struct RunConfig {
  UserId user_id{};
  std::string workbook_name{"phase4"};
  Capital workbook_capital{Capital::from_paise(10'00'00'000'00)};
  std::vector<std::string> tickers{};
  std::vector<std::string> strategies{"ema_crossover", "vwap_reversion", "consecutive_up_clip"};
  Timestamp from{};
  Timestamp to{};
  Timestamp trade_from{};
  Timestamp trade_to{};
  std::string router{"default_router"};
  std::size_t max_replay_bars{0};
};

struct RunResult {
  WorkbookId workbook_id{};
  BorrowId borrow_id{};
  Capital workbook_available_before{};
  Capital workbook_available_after{};
  Capital returned{};
  std::vector<StrategyEvalResult> evaluations{};
  std::vector<ContainerManager::Snapshot> traded{};
  int selected{0};
  int skipped{0};
  int real_containers{0};
  int fills{0};
  bool force_stopped{false};
};

class RunManager {
public:
  RunResult execute(const RunConfig& config, DataSourceRegistry& data, StrategyRegistry& strategies,
                    WorkbookManager& books, SymbolTable& symbols);
};

}  // namespace algocraft
