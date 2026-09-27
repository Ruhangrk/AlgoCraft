#include "algocraft/strategies/three_white_soldiers.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include <algorithm>
namespace algocraft {
namespace {
constexpr int kAlloc=40, kStop=40, kLast=14*60+30;
constexpr StrategyId kSid=StrategyId::from(24);
}
void ThreeWhiteSoldiers::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_=c; green_streak_=0; last_close_=0; entry_=0; session_day_=-1;
}
void ThreeWhiteSoldiers::on_fill(const FillEvent& f) {
  entry_ = f.side==Side::Buy ? f.fill_price.paise() : 0;
}
void ThreeWhiteSoldiers::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id!=config_.symbol_id || bar.resolution!=BarResolution::OneMin) return;
  const auto day=bar.timestamp.nanos()/kNanosPerDay;
  if (day!=session_day_) { session_day_=day; green_streak_=0; last_close_=0; }
  const auto o=bar.open.paise(), h=bar.high.paise(), l=bar.low.paise(), c=bar.close.paise();
  const auto body=c-o;
  const auto range=std::max<std::int64_t>(1,h-l);
  const bool strong_green = body>0 && body*2 >= range; // body ≥50% of range
  if (strong_green && (last_close_==0 || c>last_close_)) ++green_streak_;
  else green_streak_= strong_green ? 1 : 0;
  last_close_=c;
  const bool long_pos=pf.position.shares()>0;
  if (long_pos) {
    const bool red=c<o;
    const bool stop=entry_>0 && c*10000<=entry_*(10000-kStop);
    if (red || stop) out.push_back(make_intent(kSid,config_.symbol_id,Side::Sell,pf.position));
    return;
  }
  if (ist_minute_of_day(bar.timestamp)>=kLast || green_streak_<3) return;
  const auto qty=position_for_capital(Capital::from_paise(pf.cash.paise()*kAlloc/100), bar.close, 1);
  if (qty.shares()>0) out.push_back(make_intent(kSid,config_.symbol_id,Side::Buy,qty));
  green_streak_=0; // one-shot per sequence
}
StrategyMetadata ThreeWhiteSoldiers::metadata() const {
  return {"three_white_soldiers","1.0.0",TradingMode::Mis,BarResolution::OneMin,{}};
}
}  // namespace algocraft
