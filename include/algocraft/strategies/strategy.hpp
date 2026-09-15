#pragma once

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
};

struct StrategyConfig {
  SymbolId symbol_id{0};
  Quantity order_qty{Quantity::from_shares(1)};
  int ema_fast{9};
  int ema_slow{21};
  int rsi_period{14};
  int orb_bars{3};
  int vwap_dev_paise{50};
};

class Strategy {
public:
  virtual ~Strategy() = default;

  virtual void configure(const StrategyConfig& config, IndicatorLibrary& lib) = 0;
  virtual std::vector<OrderIntent> on_bar(const BarEvent& bar, const PortfolioView& portfolio) = 0;
  virtual void on_fill(const FillEvent& fill) = 0;
  virtual void on_order_update(const OrderUpdate& update) = 0;
  [[nodiscard]] virtual bool should_exit() const = 0;
  [[nodiscard]] virtual StrategyMetadata metadata() const = 0;
};

}  // namespace algocraft
