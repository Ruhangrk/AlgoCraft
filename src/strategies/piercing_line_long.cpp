#include "algocraft/strategies/piercing_line_long.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
namespace {
constexpr int kAlloc=40, kTp=45, kStop=30, kLast=14*60+30;
constexpr StrategyId kSid=StrategyId::from(21);
}
void PiercingLineLong::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_=c; have_=false; entry_=0; session_day_=-1;
}
void PiercingLineLong::on_fill(const FillEvent& f) {
  entry_ = f.side==Side::Buy ? f.fill_price.paise() : 0;
}
void PiercingLineLong::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id!=config_.symbol_id || bar.resolution!=BarResolution::OneMin) return;
  const auto day=bar.timestamp.nanos()/kNanosPerDay;
  if (day!=session_day_) { session_day_=day; have_=false; }
  const auto o=bar.open.paise(), c=bar.close.paise();
  const bool long_pos=pf.position.shares()>0;
  if (long_pos && entry_>0) {
    if (c*10000>=entry_*(10000+kTp) || c*10000<=entry_*(10000-kStop))
      out.push_back(make_intent(kSid,config_.symbol_id,Side::Sell,pf.position));
    po_=o; pc_=c; have_=true; return;
  }
  if (!long_pos && have_ && ist_minute_of_day(bar.timestamp)<kLast) {
    const bool prev_red = pc_ < po_;
    const bool cur_green = c > o;
    const auto mid = (po_ + pc_) / 2;
    // Relaxed piercing (intraday): open near/below prior close, close above midpoint.
    if (prev_red && cur_green && o <= pc_ && c > mid && c < po_) {
      const auto qty=position_for_capital(Capital::from_paise(pf.cash.paise()*kAlloc/100), bar.close, 1);
      if (qty.shares()>0) out.push_back(make_intent(kSid,config_.symbol_id,Side::Buy,qty));
    }
  }
  po_=o; pc_=c; have_=true;
}
StrategyMetadata PiercingLineLong::metadata() const {
  return {"piercing_line_long","1.0.0",TradingMode::Mis,BarResolution::OneMin,{}};
}
}  // namespace algocraft
