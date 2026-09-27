#pragma once

#include "algocraft/routing/routing_algo.hpp"

namespace algocraft {

// 1-week (5 NSE sessions) eval → top 15 (stock, strategy) by PnL → equal Real capital.
class Top15WeekRouter final : public RoutingAlgo {
public:
  void configure(const RoutingConfig& config) override;
  void start(DataSourceRegistry& data, StrategyRegistry& strategies,
             ContainerManager& containers) override;
  [[nodiscard]] RouterDefaults defaults() const override;
  [[nodiscard]] std::string name() const override { return "top15_week_router"; }

private:
  void evaluate_all(DataSourceRegistry& data, StrategyRegistry& strategies);
  void select_top_n(std::size_t n);

  RoutingConfig config_{};
  static constexpr std::size_t kTopN = 15;
};

}  // namespace algocraft
