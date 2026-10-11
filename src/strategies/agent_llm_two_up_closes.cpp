#include "algocraft/strategies/agent_llm_two_up_closes.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
void AgentLlmTwoUpCloses::configure(const StrategyConfig& cfg, IndicatorLibrary&) { config_ = cfg; }
StrategyMetadata AgentLlmTwoUpCloses::metadata() const {
  return {"agent_llm_two_up_closes", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}
void AgentLlmTwoUpCloses::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  int minute = ist_minute_of_day(bar.timestamp);
  if (minute >= 15 * 60 + 15) {
    if (pf.position.shares() > 0) {
      out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Sell, Quantity::from_shares(pf.position.shares())));
    }
    return;
  }
  if (prev_close_valid_ && bar.close > prev_close_) {
    up_streak_++;
  } else {
    up_streak_ = 0;
  }
  if (pf.position.shares() == 0 && up_streak_ >= 2) {
    out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Buy, Quantity::from_shares(1)));
  } else if (pf.position.shares() > 0 && bar.close < prev_close_) {
    out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Sell, Quantity::from_shares(pf.position.shares())));
  }
  prev_close_ = bar.close;
  prev_close_valid_ = true;
}
}