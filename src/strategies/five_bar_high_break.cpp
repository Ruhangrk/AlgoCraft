#include "algocraft/strategies/five_bar_high_break.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include <algorithm>
namespace algocraft {
namespace {
constexpr int kAllocPct = 40; constexpr int kTpBps = 50; constexpr int kLastHour = 14 * 60 + 30;
constexpr StrategyId kSid = StrategyId::from(15);
}
void FiveBarHighBreak::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_ = c; n_ = 0; entry_paise_ = 0; session_day_ = -1; highs_ = {}; lows_ = {};
}
void FiveBarHighBreak::on_fill(const FillEvent& f) {
  if (f.side == Side::Buy) entry_paise_ = f.fill_price.paise(); else entry_paise_ = 0;
}
void FiveBarHighBreak::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  const auto day = bar.timestamp.nanos() / kNanosPerDay;
  if (day != session_day_) { session_day_ = day; n_ = 0; highs_ = {}; lows_ = {}; }
  const auto h = bar.high.paise(), l = bar.low.paise(), c = bar.close.paise();
  const bool long_pos = pf.position.shares() > 0;
  if (long_pos && n_ >= 3) {
    const auto stop_lvl = *std::min_element(lows_.begin(), lows_.begin() + 3);
    const bool tp = entry_paise_ > 0 && c * 10000 >= entry_paise_ * (10000 + kTpBps);
    if (c < stop_lvl || tp) out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
  } else if (!long_pos && n_ >= 5 && ist_minute_of_day(bar.timestamp) < kLastHour) {
    const auto brk = *std::max_element(highs_.begin(), highs_.end());
    if (c > brk) {
      const auto qty = position_for_capital(Capital::from_paise(pf.cash.paise() * kAllocPct / 100), bar.close, 1);
      if (qty.shares() > 0) out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, qty));
    }
  }
  if (n_ < 5) { highs_[n_] = h; lows_[n_] = l; ++n_; }
  else {
    for (int i = 0; i < 4; ++i) { highs_[i] = highs_[i + 1]; lows_[i] = lows_[i + 1]; }
    highs_[4] = h; lows_[4] = l;
  }
}
StrategyMetadata FiveBarHighBreak::metadata() const {
  return {"five_bar_high_break", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}
}  // namespace algocraft
