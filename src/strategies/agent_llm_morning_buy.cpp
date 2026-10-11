#include "algocraft/strategies/agent_llm_morning_buy.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
void AgentLlmMorningBuy::configure(const StrategyConfig& cfg, IndicatorLibrary&) { config_ = cfg; bought_ = false; }
void AgentLlmMorningBuy::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  int m = ist_minute_of_day(bar.timestamp);
  auto sid = StrategyId::from(1);
  if (m >= 15*60+15 && pf.position.shares() != 0) {
    out.push_back(make_intent(sid, config_.symbol_id, Side::Sell, Quantity::from_shares(1)));
    bought_ = false;
  } else if (m >= 9*60+30 && pf.position.shares() == 0 && !bought_) {
    out.push_back(make_intent(sid, config_.symbol_id, Side::Buy, Quantity::from_shares(1)));
    bought_ = true;
  }
}
StrategyMetadata AgentLlmMorningBuy::metadata() const { return {"agent_llm_morning_buy", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}}; }
}