#include "algocraft/strategies/agent_llm_red_exit.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
void AgentLlmRedExit::configure(const StrategyConfig& cfg, IndicatorLibrary&) { config_ = cfg; }
void AgentLlmRedExit::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  int min = ist_minute_of_day(bar.timestamp);
  if (min >= 15*60 + 15) {
    if (pf.position.shares() > 0) out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Sell, Quantity::from_shares(1)));
    return;
  }
  bool green = bar.close > bar.open;
  bool red = bar.close < bar.open;
  if (pf.position.shares() == 0 && green) {
    out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Buy, Quantity::from_shares(1)));
  } else if (pf.position.shares() > 0 && red) {
    out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Sell, Quantity::from_shares(1)));
  }
}
StrategyMetadata AgentLlmRedExit::metadata() const { return {"agent_llm_red_exit", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}}; }
}