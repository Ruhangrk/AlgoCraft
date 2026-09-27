#include "algocraft/strategies/compression_break.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include <algorithm>
namespace algocraft {
namespace {
constexpr int kAllocPct = 40; constexpr int kTinyBps = 10; constexpr int kBreakBps = 20;
constexpr int kTpBps = 40; constexpr int kStopBps = 30; constexpr int kLastHour = 14 * 60 + 30;
constexpr StrategyId kSid = StrategyId::from(16);
}
void CompressionBreak::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_ = c; n_ = 0; entry_paise_ = 0; session_day_ = -1;
}
void CompressionBreak::on_fill(const FillEvent& f) {
  if (f.side == Side::Buy) entry_paise_ = f.fill_price.paise(); else entry_paise_ = 0;
}
void CompressionBreak::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  const auto day = bar.timestamp.nanos() / kNanosPerDay;
  if (day != session_day_) { session_day_ = day; n_ = 0; }
  const auto h = bar.high.paise(), l = bar.low.paise(), c = bar.close.paise();
  const bool long_pos = pf.position.shares() > 0;
  if (long_pos && entry_paise_ > 0) {
    if (c * 10000 >= entry_paise_ * (10000 + kTpBps) || c * 10000 <= entry_paise_ * (10000 - kStopBps)) {
      out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    }
  } else if (!long_pos && n_ >= 4 && ist_minute_of_day(bar.timestamp) < kLastHour) {
    bool tiny = true;
    std::int64_t mx = highs_[0], mn = lows_[0];
    for (int i = 0; i < 4; ++i) {
      const auto mid = (highs_[i] + lows_[i]) / 2;
      if (mid <= 0 || (highs_[i] - lows_[i]) * 10000 > mid * kTinyBps) tiny = false;
      mx = std::max(mx, highs_[i]); mn = std::min(mn, lows_[i]);
    }
    if (tiny && mx > 0 && c * 10000 >= mx * (10000 + kBreakBps)) {
      const auto qty = position_for_capital(Capital::from_paise(pf.cash.paise() * kAllocPct / 100), bar.close, 1);
      if (qty.shares() > 0) out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, qty));
    }
  }
  if (n_ < 4) { highs_[n_] = h; lows_[n_] = l; ++n_; }
  else {
    for (int i = 0; i < 3; ++i) { highs_[i] = highs_[i + 1]; lows_[i] = lows_[i + 1]; }
    highs_[3] = h; lows_[3] = l;
  }
}
StrategyMetadata CompressionBreak::metadata() const {
  return {"compression_break", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}
}  // namespace algocraft
