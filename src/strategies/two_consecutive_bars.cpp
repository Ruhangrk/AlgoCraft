#include "algocraft/strategies/two_consecutive_bars.hpp"

#include <algorithm>

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/indicators/daily_sma_warmup.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {
namespace {

constexpr int kLastHourMinute = 14 * 60 + 30;  // 14:30 IST — no new buys
constexpr int kDeployAllocPct = 10;
// Break-even band: max(₹1, 0.05% of cost).
constexpr std::int64_t kBeMinTolPaise = 100;
constexpr StrategyId kSid = StrategyId::from(6);

inline bool green(std::int64_t o, std::int64_t c) { return c > o; }
inline bool red(std::int64_t o, std::int64_t c) { return c < o; }

}  // namespace

void TwoConsecutiveBars::reset_cycle() {
  total_cost_paise_ = 0;
}

void TwoConsecutiveBars::configure(const StrategyConfig& config, IndicatorLibrary& lib) {
  config_ = config;
  const auto period = config_.sma_period > 0 ? config_.sma_period : kDefaultSmaPeriod;
  sma200_ = &lib.get<Sma>(config_.symbol_id, BarResolution::OneDay, period);
  session_day_ = -1;
  candle_count_ = 0;
  candles_ = {};
  reset_cycle();
}

void TwoConsecutiveBars::push_candle(const BarEvent& bar) {
  Candle c{.open = bar.open.paise(),
           .high = bar.high.paise(),
           .low = bar.low.paise(),
           .close = bar.close.paise()};
  if (candle_count_ < 3) {
    candles_[static_cast<std::size_t>(candle_count_++)] = c;
    return;
  }
  candles_[0] = candles_[1];
  candles_[1] = candles_[2];
  candles_[2] = c;
}

Quantity TwoConsecutiveBars::buy_clip(Price px) const {
  const auto alloc = config_.alloc_paise > 0 ? config_.alloc_paise : config_.clip_paise;
  if (alloc <= 0 || px.paise() <= 0) {
    return {};
  }
  const auto clip = Capital::from_paise(alloc * kDeployAllocPct / 100);
  // Spend that cash as notional (no extra MIS leverage on the clip).
  return position_for_capital(clip, px, /*leverage=*/1);
}

bool TwoConsecutiveBars::three_green() const {
  if (candle_count_ < 3) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    const auto& c = candles_[static_cast<std::size_t>(i)];
    if (!green(c.open, c.close)) {
      return false;
    }
  }
  return true;
}

bool TwoConsecutiveBars::three_red() const {
  if (candle_count_ < 3) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    const auto& c = candles_[static_cast<std::size_t>(i)];
    if (!red(c.open, c.close)) {
      return false;
    }
  }
  return true;
}

bool TwoConsecutiveBars::two_red() const {
  if (candle_count_ < 2) {
    return false;
  }
  const int i0 = candle_count_ >= 3 ? 1 : 0;
  const int i1 = candle_count_ >= 3 ? 2 : 1;
  const auto& a = candles_[static_cast<std::size_t>(i0)];
  const auto& b = candles_[static_cast<std::size_t>(i1)];
  return red(a.open, a.close) && red(b.open, b.close);
}

bool TwoConsecutiveBars::two_green_range() const {
  if (candle_count_ < 2) {
    return false;
  }
  // Last two candles in the ring.
  const int i0 = candle_count_ >= 3 ? 1 : 0;
  const int i1 = candle_count_ >= 3 ? 2 : 1;
  const auto& a = candles_[static_cast<std::size_t>(i0)];
  const auto& b = candles_[static_cast<std::size_t>(i1)];
  if (!green(a.open, a.close) || !green(b.open, b.close)) {
    return false;
  }
  if (a.low <= 0) {
    return false;
  }
  // (high2 - low1) / low1 >= 0.25%  <=>  (high2 - low1) * 10000 >= low1 * 25
  const auto rise = b.high - a.low;
  if (rise <= 0) {
    return false;
  }
  return rise * 10000 >= a.low * 25;
}

bool TwoConsecutiveBars::break_even_at(Price sp, Quantity pos) const {
  if (pos.shares() <= 0 || total_cost_paise_ <= 0 || sp.paise() <= 0) {
    return false;
  }
  const auto sell_fee = costs_.sell_fees(sp, pos).paise();
  const auto proceeds = notional(sp, pos).paise() - sell_fee;
  const auto tol = std::max(kBeMinTolPaise, total_cost_paise_ * 5 / 10000);
  const auto diff = proceeds - total_cost_paise_;
  return diff >= -tol && diff <= tol;
}

bool TwoConsecutiveBars::above_sma(Price px) const {
  if (sma200_ == nullptr || !sma200_->ready() || px.paise() <= 0) {
    return false;
  }
  return static_cast<double>(px.paise()) > sma200_->value();
}

void TwoConsecutiveBars::on_bar(const BarEvent& bar, const PortfolioView& portfolio,
                                std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id) {
    return;
  }
  // Strategy signals are 1m; daily bars only seed SMA via warmup.
  if (bar.resolution != BarResolution::OneMin) {
    return;
  }

  const auto day = bar.timestamp.nanos() / kNanosPerDay;
  if (day != session_day_) {
    session_day_ = day;
    candle_count_ = 0;
    candles_ = {};
  }

  push_candle(bar);

  const auto minute = ist_minute_of_day(bar.timestamp);
  const bool allow_buy = minute < kLastHourMinute;
  const bool long_pos = portfolio.position.shares() > 0;

  // Sell: 3 reds, or break-even only after 2 consecutive reds (exit on the 2nd).
  const bool sell_signal =
      long_pos && (three_red() || (two_red() && break_even_at(bar.close, portfolio.position)));
  if (sell_signal) {
    out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, portfolio.position));
    return;  // same-bar: sell wins; no buy
  }

  if (!allow_buy || !above_sma(bar.close)) {
    return;
  }
  const bool buy_signal = three_green() || two_green_range();
  if (!buy_signal) {
    return;
  }
  const auto qty = buy_clip(bar.close);
  if (qty.shares() <= 0) {
    return;
  }
  out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, qty));
}

void TwoConsecutiveBars::on_fill(const FillEvent& fill) {
  if (fill.side == Side::Buy) {
    total_cost_paise_ += notional(fill.fill_price, fill.filled_qty).paise() + fill.fees.paise();
    return;
  }
  reset_cycle();
}

StrategyMetadata TwoConsecutiveBars::metadata() const {
  return {.name = "two_consecutive_bars",
          .version = "1.1.0",
          .trading_mode = TradingMode::Mis,
          .required_resolution = BarResolution::OneMin,
          .required_indicators = {"SMA"}};
}

}  // namespace algocraft
