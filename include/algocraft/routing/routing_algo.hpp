#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

class ContainerManager;
class DataSourceRegistry;
class StrategyRegistry;

struct RoutingConfig {
  WorkbookId workbook_id{};
  std::vector<SymbolId> stocks{};
  std::vector<std::string> strategies{};
  Timestamp from{};
  Timestamp to{};
  Capital eval_capital{Capital::from_paise(10'00'000'00)};
};

struct StrategyEvalResult {
  SymbolId symbol_id{0};
  std::string ticker;
  std::string strategy_name;
  StrategyId strategy_id{};
  std::int64_t pnl_paise{0};
  int fills{0};
  std::size_t bars{0};
  Price last_price{};
  bool selected{false};
};

class RoutingAlgo {
public:
  virtual ~RoutingAlgo() = default;

  virtual void configure(const RoutingConfig& config) = 0;
  virtual void start(DataSourceRegistry& data, StrategyRegistry& strategies,
                     ContainerManager& containers) {
    (void)data;
    (void)strategies;
    (void)containers;
  }
  virtual void on_bar(const BarEvent& bar) { (void)bar; }
  virtual void on_session_start() {}
  virtual void on_session_end() {}
  virtual void stop() {}
  [[nodiscard]] virtual const std::vector<StrategyEvalResult>& evaluations() const {
    return evaluations_;
  }
  [[nodiscard]] virtual std::string name() const = 0;

protected:
  std::vector<StrategyEvalResult> evaluations_{};
};

}  // namespace algocraft
