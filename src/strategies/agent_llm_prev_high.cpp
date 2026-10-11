#include "algocraft/strategies/agent_llm_prev_high.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
void AgentLlmPrevHigh::configure(const StrategyConfig& cfg, IndicatorLibrary&) { config_ = cfg; }
void AgentLlmPrevHigh::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  int min = ist_minute_of_day(bar.timestamp);
  if (min >= 15*60 + 15) {
    if (pf.position.shares() > 0) out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Sell, Quantity::from_shares(1)));
    return;
  }
  if (prev_high_ == Price{}) { prev_high_ = bar.high; return; }
  bool flat = pf.position.shares() == 0;
  if (flat && bar.close > prev_high_) {
    out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Buy, Quantity::from_shares(1)));
  } else if (!flat && bar.close < prev_high_) {
    out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Sell, Quantity::from_shares(1)));
  }
  if (bar.high > prev_high_) prev_high_ = bar.high;
}
StrategyMetadata AgentLlmPrevHigh::metadata() const { return {"agent_llm_prev_high", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}}; }
}