#pragma once

#include "algocraft/routing/routing_algo.hpp"

namespace algocraft {

// Eval window BACKTEST of every (stock, strategy). Profit → REAL on the trade window, skip PAPER.
class DefaultRouter final : public RoutingAlgo {
public:
  void configure(const RoutingConfig& config) override;
  void start(DataSourceRegistry& data, StrategyRegistry& strategies,
             ContainerManager& containers) override;
  [[nodiscard]] std::string name() const override { return "default_router"; }

private:
  void evaluate_all(DataSourceRegistry& data, StrategyRegistry& strategies);

  RoutingConfig config_{};
};

}  // namespace algocraft
