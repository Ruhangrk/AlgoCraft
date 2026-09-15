#include "algocraft/strategies/consecutive_up_clip.hpp"

#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {
namespace {

constexpr std::int64_t kNanosPerDay = 86'400'000'000'000LL;
constexpr std::int64_t kIstOffsetSec = 5 * 3600 + 30 * 60;
constexpr int kNoAddAfterMinute = 13 * 60;
constexpr int kLastHourMinute = 14 * 60 + 30;

int ist_minute_of_day(Timestamp ts) {
  const auto sec = ts.nanos() / 1'000'000'000LL;
  auto ist = sec + kIstOffsetSec;
  auto tod = ist % 86400;
  if (tod < 0) {
    tod += 86400;
  }
  return static_cast<int>(tod / 60);
}

std::int64_t plus_bps(std::int64_t price, int bps) {
  return price + (price * static_cast<std::int64_t>(bps)) / 10000;
}

std::int64_t minus_bps(std::int64_t price, int bps) {
  return price - (price * static_cast<std::int64_t>(bps)) / 10000;
}

}  // namespace

void ConsecutiveUpClip::reset_cycle() {
  up_streak_ = 0;
  recover_streak_ = 0;
  p0_paise_ = 0;
  min_close_since_entry_ = 0;
  dipped_since_clip_ = false;
  add_disqualified_ = false;
}

void ConsecutiveUpClip::reset_session() {
  reset_cycle();
  have_prev_ = false;
  prev_close_ = 0;
}

void ConsecutiveUpClip::configure(const StrategyConfig& config, IndicatorLibrary& lib) {
  config_ = config;
  (void)lib;
  session_day_ = -1;
  reset_session();
}

Quantity ConsecutiveUpClip::clip_qty(Price px) const {
  if (px.paise() <= 0 || config_.clip_paise <= 0) {
    return {};
  }
  auto shares = config_.clip_paise / px.paise();
  if (shares <= 0) {
    shares = 1;
  }
  return Quantity::from_shares(shares);
}

OrderIntent ConsecutiveUpClip::sell_all(Quantity pos) const {
  return make_intent(StrategyId::from(4), config_.symbol_id, Side::Sell, pos);
}

void ConsecutiveUpClip::on_bar(const BarEvent& bar, const PortfolioView& portfolio,
                               std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id) {
    return;
  }

  const auto day = bar.timestamp.nanos() / kNanosPerDay;
  if (day != session_day_) {
    session_day_ = day;
    reset_session();
  }

  const auto close = bar.close.paise();
  const auto minute = ist_minute_of_day(bar.timestamp);
  const bool last_hour = minute >= kLastHourMinute;
  const bool allow_add = minute < kNoAddAfterMinute;
  const bool allow_first = minute < kLastHourMinute;
  const bool long_pos = portfolio.position.shares() > 0;

  if (have_prev_) {
    if (close > prev_close_) {
      ++up_streak_;
      ++recover_streak_;
    } else {
      up_streak_ = 0;
      recover_streak_ = 0;
    }
  }

  if (long_pos && p0_paise_ > 0) {
    if (min_close_since_entry_ == 0 || close < min_close_since_entry_) {
      min_close_since_entry_ = close;
    }
    if (close < p0_paise_) {
      dipped_since_clip_ = true;
    }
    if (min_close_since_entry_ < minus_bps(p0_paise_, config_.add_max_dip_bps)) {
      add_disqualified_ = true;
    }

    if (last_hour && have_prev_ && close < prev_close_) {
      out.push_back(sell_all(portfolio.position));
      prev_close_ = close;
      have_prev_ = true;
      return;
    }
    if (close >= plus_bps(p0_paise_, config_.take_profit_bps)) {
      out.push_back(sell_all(portfolio.position));
      prev_close_ = close;
      have_prev_ = true;
      return;
    }
    if (close <= minus_bps(p0_paise_, config_.stop_bps)) {
      out.push_back(sell_all(portfolio.position));
      prev_close_ = close;
      have_prev_ = true;
      return;
    }
    if (allow_add && !last_hour && !add_disqualified_ && dipped_since_clip_ &&
        recover_streak_ >= config_.add_up_bars) {
      auto buy = make_intent(StrategyId::from(4), config_.symbol_id, Side::Buy, clip_qty(bar.close));
      out.push_back(buy);
      recover_streak_ = 0;
      dipped_since_clip_ = false;
    }
  } else if (!long_pos && allow_first && !last_hour && have_prev_ &&
             up_streak_ >= config_.entry_up_bars) {
    out.push_back(make_intent(StrategyId::from(4), config_.symbol_id, Side::Buy, clip_qty(bar.close)));
    up_streak_ = 0;
  }

  prev_close_ = close;
  have_prev_ = true;
}

void ConsecutiveUpClip::on_fill(const FillEvent& fill) {
  if (fill.side == Side::Buy) {
    if (p0_paise_ == 0) {
      p0_paise_ = fill.fill_price.paise();
      min_close_since_entry_ = fill.fill_price.paise();
    }
    dipped_since_clip_ = false;
    recover_streak_ = 0;
    add_disqualified_ = min_close_since_entry_ > 0 &&
                        min_close_since_entry_ < minus_bps(p0_paise_, config_.add_max_dip_bps);
    return;
  }
  reset_cycle();
}

StrategyMetadata ConsecutiveUpClip::metadata() const {
  return {.name = "consecutive_up_clip",
          .version = "1.0.0",
          .trading_mode = TradingMode::Mis,
          .required_resolution = BarResolution::OneMin,
          .required_indicators = {}};
}

}  // namespace algocraft
