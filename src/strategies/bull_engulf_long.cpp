#include "algocraft/strategies/bull_engulf_long.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
namespace {
constexpr int kAllocPct = 40; constexpr int kTpBps = 45; constexpr int kStopBps = 30;
constexpr int kLastHour = 14 * 60 + 30; constexpr StrategyId kSid = StrategyId::from(11);
}
void BullEngulfLong::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_ = c; have_prev_ = false; entry_paise_ = 0; session_day_ = -1;
}
void BullEngulfLong::on_fill(const FillEvent& f) {
  if (f.side == Side::Buy) entry_paise_ = f.fill_price.paise(); else entry_paise_ = 0;
}
void BullEngulfLong::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  const auto day = bar.timestamp.nanos() / kNanosPerDay;
  if (day != session_day_) { session_day_ = day; have_prev_ = false; }
  const auto o = bar.open.paise(), c = bar.close.paise();
  const bool long_pos = pf.position.shares() > 0;
  if (long_pos && entry_paise_ > 0) {
    if (c * 10000 >= entry_paise_ * (10000 + kTpBps) || c * 10000 <= entry_paise_ * (10000 - kStopBps)) {
      out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    }
    prev_o_ = o; prev_c_ = c; have_prev_ = true; return;
  }
  if (!long_pos && have_prev_ && ist_minute_of_day(bar.timestamp) < kLastHour) {
    const bool prev_red = prev_c_ < prev_o_;
    const bool cur_green = c > o;
    const bool engulfs = o <= prev_c_ && c >= prev_o_;
    if (prev_red && cur_green && engulfs) {
      const auto qty = position_for_capital(Capital::from_paise(pf.cash.paise() * kAllocPct / 100), bar.close, 1);
      if (qty.shares() > 0) out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, qty));
    }
  }
  prev_o_ = o; prev_c_ = c; have_prev_ = true;
}
StrategyMetadata BullEngulfLong::metadata() const {
  return {"bull_engulf_long", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}
}  // namespace algocraft
