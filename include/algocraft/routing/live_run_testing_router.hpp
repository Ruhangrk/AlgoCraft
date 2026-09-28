#pragma once

#include "algocraft/routing/routing_algo.hpp"

namespace algocraft {

// Eval live_run_testing on 15 stocks for 1 session (last day) → top 3 by PnL (incl. least loss).
class LiveRunTestingRouter final : public RoutingAlgo {
public:
  void configure(const RoutingConfig& config) override;
  void start(DataSourceRegistry& data, StrategyRegistry& strategies,
             ContainerManager& containers) override;
  [[nodiscard]] RouterDefaults defaults() const override;
  [[nodiscard]] std::string name() const override { return "live_run_testing_router"; }

private:
  void evaluate_all(DataSourceRegistry& data, StrategyRegistry& strategies);
  void select_top_n(std::size_t n);

  RoutingConfig config_{};
  static constexpr std::size_t kTopN = 3;
};

}  // namespace algocraft
