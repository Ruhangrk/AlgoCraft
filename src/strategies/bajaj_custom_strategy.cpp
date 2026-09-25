#include "algocraft/strategies/bajaj_custom_strategy.hpp"

#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {
namespace {

// Tuned on BAJFINANCE 1m (≈1 week sample).
constexpr double kRsiEntry = 32.0;
constexpr double kRsiExit = 60.0;
constexpr int kVwapEntryBps = 25;
constexpr int kTakeProfitBps = 30;
constexpr int kStopBps = 25;
constexpr int kEntryStartMinute = 9 * 60 + 30;  // 09:30 IST
constexpr int kEntryEndMinute = 13 * 60;        // 13:00 IST
constexpr int kFlattenMinute = 14 * 60 + 30;    // 14:30 IST

// Moderate risk for this name/strategy: deploy 40% of cash as MIS margin.
// With ~5× MIS leverage that is ~2× cash notional, leaving headroom for fees/SL.
constexpr int kDeployCashPct = 40;
constexpr int kMisLeverage = 5;

std::int64_t plus_bps(std::int64_t price, int bps) {
  return price + (price * static_cast<std::int64_t>(bps)) / 10000;
}

std::int64_t minus_bps(std::int64_t price, int bps) {
  return price - (price * static_cast<std::int64_t>(bps)) / 10000;
}

}  // namespace

void BajajCustomStrategy::reset_session() {
  traded_today_ = false;
  entry_paise_ = 0;
}

Quantity BajajCustomStrategy::size_for_cash(Capital cash, Price px) const {
  if (cash.paise() <= 0 || px.paise() <= 0) {
    return {};
  }
  const auto margin = Capital::from_paise(cash.paise() * kDeployCashPct / 100);
  return position_for_capital(margin, px, kMisLeverage);
}

void BajajCustomStrategy::configure(const StrategyConfig& config, IndicatorLibrary& lib) {
  config_ = config;
  const int rsi_period = config.rsi_period > 0 ? config.rsi_period : 14;
  rsi_ = &lib.get<Rsi>(config.symbol_id, BarResolution::OneMin, rsi_period);
  vwap_ = &lib.get<Vwap>(config.symbol_id, BarResolution::OneMin);
  session_day_ = -1;
  reset_session();
}

void BajajCustomStrategy::on_bar(const BarEvent& bar, const PortfolioView& portfolio,
                                 std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || rsi_ == nullptr || vwap_ == nullptr) {
    return;
  }

  const auto day = bar.timestamp.nanos() / kNanosPerDay;
  if (day != session_day_) {
    session_day_ = day;
    reset_session();
  }

  if (!rsi_->ready() || !vwap_->ready()) {
    return;
  }

  const auto minute = ist_minute_of_day(bar.timestamp);
  const auto close = bar.close.paise();
  const auto vwap = static_cast<std::int64_t>(vwap_->value());
  const auto rsi = rsi_->value();
  const bool long_pos = portfolio.position.shares() > 0;

  if (long_pos && entry_paise_ > 0) {
    const bool hit_tp = close >= plus_bps(entry_paise_, kTakeProfitBps);
    const bool hit_sl = close <= minus_bps(entry_paise_, kStopBps);
    const bool hit_rsi = rsi >= kRsiExit;
    const bool hit_vwap = close >= vwap;
    const bool hit_flat = minute >= kFlattenMinute;
    if (hit_tp || hit_sl || hit_rsi || hit_vwap || hit_flat) {
      out.push_back(make_intent(StrategyId::from(5), config_.symbol_id, Side::Sell,
                                portfolio.position));
    }
    return;
  }

  if (long_pos || traded_today_) {
    return;
  }
  if (minute < kEntryStartMinute || minute >= kEntryEndMinute) {
    return;
  }
  if (rsi >= kRsiEntry) {
    return;
  }
  if (vwap <= 0) {
    return;
  }
  const auto thresh = minus_bps(vwap, kVwapEntryBps);
  if (close > thresh) {
    return;
  }

  const auto qty = size_for_cash(portfolio.cash, bar.close);
  if (qty.shares() <= 0) {
    return;
  }
  out.push_back(make_intent(StrategyId::from(5), config_.symbol_id, Side::Buy, qty));
}

void BajajCustomStrategy::on_fill(const FillEvent& fill) {
  if (fill.side == Side::Buy) {
    entry_paise_ = fill.fill_price.paise();
    traded_today_ = true;
    return;
  }
  entry_paise_ = 0;
}

StrategyMetadata BajajCustomStrategy::metadata() const {
  return {.name = "bajaj_custom_strategy",
          .version = "1.0.0",
          .trading_mode = TradingMode::Mis,
          .required_resolution = BarResolution::OneMin,
          .required_indicators = {"RSI", "VWAP"}};
}

}  // namespace algocraft
