#include "algocraft/strategies/atr_expansion_long.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include <algorithm>
#include <cmath>
namespace algocraft {
namespace {
constexpr int kAlloc=40, kLast=14*60+30;
constexpr StrategyId kSid=StrategyId::from(25);
}
void AtrExpansionLong::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_=c; n_=0; atr_=0; prev_close_=0; entry_=0; session_day_=-1; tr_={};
}
void AtrExpansionLong::on_fill(const FillEvent& f) {
  entry_ = f.side==Side::Buy ? f.fill_price.paise() : 0;
}
void AtrExpansionLong::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id!=config_.symbol_id || bar.resolution!=BarResolution::OneMin) return;
  const auto day=bar.timestamp.nanos()/kNanosPerDay;
  if (day!=session_day_) { session_day_=day; /* keep ATR across day */ }
  const auto h=static_cast<double>(bar.high.paise()), l=static_cast<double>(bar.low.paise());
  const auto c=static_cast<double>(bar.close.paise()), o=static_cast<double>(bar.open.paise());
  double tr = h - l;
  if (prev_close_ > 0) {
    tr = std::max({tr, std::fabs(h - prev_close_), std::fabs(l - prev_close_)});
  }
  if (n_ < 14) { tr_[n_++] = tr; }
  else {
    for (int i=0;i<13;++i) tr_[i]=tr_[i+1];
    tr_[13]=tr;
  }
  if (n_ >= 14) {
    double s=0; for (double x: tr_) s+=x; atr_ = s/14.0;
  }
  const bool long_pos=pf.position.shares()>0;
  if (long_pos && entry_>0 && atr_>0) {
    if (c >= entry_ + 1.5*atr_ || c <= entry_ - atr_)
      out.push_back(make_intent(kSid,config_.symbol_id,Side::Sell,pf.position));
  } else if (!long_pos && n_>=14 && atr_>0 && ist_minute_of_day(bar.timestamp)<kLast) {
    if (tr >= 1.8*atr_ && c > o) {
      const auto qty=position_for_capital(Capital::from_paise(pf.cash.paise()*kAlloc/100), bar.close, 1);
      if (qty.shares()>0) out.push_back(make_intent(kSid,config_.symbol_id,Side::Buy,qty));
    }
  }
  prev_close_ = c;
}
StrategyMetadata AtrExpansionLong::metadata() const {
  return {"atr_expansion_long","1.0.0",TradingMode::Mis,BarResolution::OneMin,{}};
}
}  // namespace algocraft
