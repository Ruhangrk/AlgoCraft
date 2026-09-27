#include "algocraft/strategies/two_green_thrust.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
namespace {
constexpr int kAllocPct = 40; constexpr int kStopBps = 35; constexpr int kLastHour = 14 * 60 + 30;
constexpr StrategyId kSid = StrategyId::from(17);
}
void TwoGreenThrust::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_ = c; have_prev_ = false; entry_paise_ = 0; session_day_ = -1;
}
void TwoGreenThrust::on_fill(const FillEvent& f) {
  if (f.side == Side::Buy) entry_paise_ = f.fill_price.paise(); else entry_paise_ = 0;
}
void TwoGreenThrust::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  const auto day = bar.timestamp.nanos() / kNanosPerDay;
  if (day != session_day_) { session_day_ = day; have_prev_ = false; }
  const auto o = bar.open.paise(), c = bar.close.paise();
  const bool long_pos = pf.position.shares() > 0;
  if (long_pos) {
    const bool red = c < o;
    const bool stop = entry_paise_ > 0 && c * 10000 <= entry_paise_ * (10000 - kStopBps);
    if (red || stop) out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    prev_o_ = o; prev_c_ = c; have_prev_ = true; return;
  }
  if (have_prev_ && ist_minute_of_day(bar.timestamp) < kLastHour) {
    const auto b0 = prev_c_ - prev_o_;
    const auto b1 = c - o;
    if (b0 > 0 && b1 > 0 && b1 * 2 >= b0 * 3) {
      const auto qty = position_for_capital(Capital::from_paise(pf.cash.paise() * kAllocPct / 100), bar.close, 1);
      if (qty.shares() > 0) out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, qty));
    }
  }
  prev_o_ = o; prev_c_ = c; have_prev_ = true;
}
StrategyMetadata TwoGreenThrust::metadata() const {
  return {"two_green_thrust", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}
}  // namespace algocraft
