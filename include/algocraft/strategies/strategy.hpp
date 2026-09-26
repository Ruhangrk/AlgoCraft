#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/portfolio_view.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/indicators/indicator_library.hpp"

namespace algocraft {

struct StrategyMetadata {
  std::string name;
  std::string version{"1.0.0"};
  TradingMode trading_mode{TradingMode::Mis};
  BarResolution required_resolution{BarResolution::OneMin};
  std::vector<std::string> required_indicators{};
};

struct StrategyConfig {
  SymbolId symbol_id{0};
  Quantity order_qty{Quantity::from_shares(1)};
  int ema_fast{9};
  int ema_slow{21};
  int rsi_period{14};
  int orb_bars{3};
  int vwap_dev_paise{50};
  int entry_up_bars{8};
  int add_up_bars{3};
  int take_profit_bps{50};
  int stop_bps{30};
  int add_max_dip_bps{25};
  std::int64_t clip_paise{20'00'000'00};
  // Initial container allocation (paise). Strategies may size clips as a fraction of this.
  std::int64_t alloc_paise{0};
  // Daily SMA period (e.g. 200DMA). Strategies that declare SMA use this.
  int sma_period{200};
};

class Strategy {
public:
  virtual ~Strategy() = default;

  virtual void configure(const StrategyConfig& config, IndicatorLibrary& lib) = 0;
  virtual void on_bar(const BarEvent& bar, const PortfolioView& portfolio,
                      std::vector<OrderIntent>& out) = 0;
  virtual void on_fill(const FillEvent& fill) = 0;
  virtual void on_order_update(const OrderUpdate& update) = 0;
  [[nodiscard]] virtual bool should_exit() const = 0;
  [[nodiscard]] virtual StrategyMetadata metadata() const = 0;
};

}  // namespace algocraft
