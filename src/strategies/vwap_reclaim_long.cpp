#include "algocraft/strategies/vwap_reclaim_long.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
namespace {
constexpr int kAllocPct = 40; constexpr int kLastHour = 14 * 60 + 30;
constexpr StrategyId kSid = StrategyId::from(13);
}
void VwapReclaimLong::configure(const StrategyConfig& c, IndicatorLibrary& lib) {
  config_ = c; have_prev_ = false;
  vwap_ = &lib.get<Vwap>(c.symbol_id, BarResolution::OneMin);
}
void VwapReclaimLong::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  if (!vwap_ || !vwap_->ready()) return;
  const auto c = static_cast<double>(bar.close.paise());
  const auto v = vwap_->value();
  const bool below = c < v;
  const bool long_pos = pf.position.shares() > 0;
  if (long_pos && have_prev_ && !prev_below_ && below) {
    out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
  } else if (!long_pos && have_prev_ && prev_below_ && !below &&
             ist_minute_of_day(bar.timestamp) < kLastHour) {
    const auto qty = position_for_capital(Capital::from_paise(pf.cash.paise() * kAllocPct / 100), bar.close, 1);
    if (qty.shares() > 0) out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, qty));
  }
  prev_below_ = below; have_prev_ = true;
}
StrategyMetadata VwapReclaimLong::metadata() const {
  return {"vwap_reclaim_long", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {"VWAP"}};
}
}  // namespace algocraft
