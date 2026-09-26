#include "algocraft/strategies/strategy_registry.hpp"

#include "algocraft/strategies/bajaj_custom_strategy.hpp"
#include "algocraft/strategies/consecutive_up_clip.hpp"
#include "algocraft/strategies/ema_crossover.hpp"
#include "algocraft/strategies/opening_range_breakout.hpp"
#include "algocraft/strategies/reliance_prev5_avg_break.hpp"
#include "algocraft/strategies/two_consecutive_bars.hpp"
#include "algocraft/strategies/vwap_reversion.hpp"

#include <stdexcept>

namespace algocraft {

void StrategyRegistry::add(std::string name, Factory factory) {
  factories_.emplace(std::move(name), std::move(factory));
}

std::unique_ptr<Strategy> StrategyRegistry::create(std::string_view name) const {
  const auto it = factories_.find(std::string{name});
  if (it == factories_.end()) {
    throw std::invalid_argument("unknown strategy: " + std::string{name});
  }
  return it->second();
}

std::vector<std::string> StrategyRegistry::names() const {
  std::vector<std::string> out;
  out.reserve(factories_.size());
  for (const auto& [name, _] : factories_) {
    out.push_back(name);
  }
  return out;
}

void register_all_strategies(StrategyRegistry& registry) {
  registry.add("ema_crossover", [] { return std::make_unique<EmaCrossover>(); });
  registry.add("vwap_reversion", [] { return std::make_unique<VwapReversion>(); });
  registry.add("opening_range_breakout", [] { return std::make_unique<OpeningRangeBreakout>(); });
  registry.add("consecutive_up_clip", [] { return std::make_unique<ConsecutiveUpClip>(); });
  registry.add("bajaj_custom_strategy", [] { return std::make_unique<BajajCustomStrategy>(); });
  registry.add("two_consecutive_bars", [] { return std::make_unique<TwoConsecutiveBars>(); });
  registry.add("reliance_prev5_avg_break", [] { return std::make_unique<ReliancePrev5AvgBreak>(); });
}

}  // namespace algocraft
