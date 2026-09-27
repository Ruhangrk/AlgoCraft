#include "algocraft/strategies/strategy_registry.hpp"

#include "algocraft/strategies/atr_expansion_long.hpp"
#include "algocraft/strategies/bajaj_custom_strategy.hpp"
#include "algocraft/strategies/bull_engulf_long.hpp"
#include "algocraft/strategies/bull_harami_break.hpp"
#include "algocraft/strategies/compression_break.hpp"
#include "algocraft/strategies/consecutive_up_clip.hpp"
#include "algocraft/strategies/dip5_mean_revert.hpp"
#include "algocraft/strategies/ema_crossover.hpp"
#include "algocraft/strategies/ema_trend_pullback.hpp"
#include "algocraft/strategies/five_bar_high_break.hpp"
#include "algocraft/strategies/hammer_reversal.hpp"
#include "algocraft/strategies/morning_star_long.hpp"
#include "algocraft/strategies/nr7_breakout.hpp"
#include "algocraft/strategies/open_dump_fade.hpp"
#include "algocraft/strategies/opening_range_breakout.hpp"
#include "algocraft/strategies/piercing_line_long.hpp"
#include "algocraft/strategies/reliance_prev5_avg_break.hpp"
#include "algocraft/strategies/three_red_bounce.hpp"
#include "algocraft/strategies/three_white_soldiers.hpp"
#include "algocraft/strategies/two_consecutive_bars.hpp"
#include "algocraft/strategies/two_green_thrust.hpp"
#include "algocraft/strategies/vwap_reclaim_long.hpp"
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
  registry.add("dip5_mean_revert", [] { return std::make_unique<Dip5MeanRevert>(); });
  registry.add("three_red_bounce", [] { return std::make_unique<ThreeRedBounce>(); });
  registry.add("bull_engulf_long", [] { return std::make_unique<BullEngulfLong>(); });
  registry.add("ema_trend_pullback", [] { return std::make_unique<EmaTrendPullback>(); });
  registry.add("vwap_reclaim_long", [] { return std::make_unique<VwapReclaimLong>(); });
  registry.add("open_dump_fade", [] { return std::make_unique<OpenDumpFade>(); });
  registry.add("five_bar_high_break", [] { return std::make_unique<FiveBarHighBreak>(); });
  registry.add("compression_break", [] { return std::make_unique<CompressionBreak>(); });
  registry.add("two_green_thrust", [] { return std::make_unique<TwoGreenThrust>(); });
  registry.add("hammer_reversal", [] { return std::make_unique<HammerReversal>(); });
  registry.add("piercing_line_long", [] { return std::make_unique<PiercingLineLong>(); });
  registry.add("bull_harami_break", [] { return std::make_unique<BullHaramiBreak>(); });
  registry.add("morning_star_long", [] { return std::make_unique<MorningStarLong>(); });
  registry.add("three_white_soldiers", [] { return std::make_unique<ThreeWhiteSoldiers>(); });
  registry.add("atr_expansion_long", [] { return std::make_unique<AtrExpansionLong>(); });
  registry.add("nr7_breakout", [] { return std::make_unique<Nr7Breakout>(); });
}

}  // namespace algocraft
