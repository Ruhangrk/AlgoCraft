#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "algocraft/domain/symbol.hpp"
#include "algocraft/market_data/market_data_feed.hpp"

namespace algocraft {

// Connects to Virtual_Websockets (JSON text frames), same fields as Upstox LTPC:
// {"instrumentKey":"NSE_EQ|…","ltp":123.45,"ltt":1710000000000}
class VirtualLiveFeed final : public MarketDataFeed {
public:
  VirtualLiveFeed(std::string ws_url, const SymbolTable* symbols,
                  std::unordered_map<std::string, std::string> ticker_to_key);

  ~VirtualLiveFeed() override;

  void connect() override;
  void disconnect() override;
  void subscribe(SymbolId symbol_id) override;
  void unsubscribe(SymbolId symbol_id) override;

  void set_max_seconds(int seconds) { max_seconds_ = seconds; }

private:
  void run_loop();
  [[nodiscard]] std::string instrument_key(SymbolId id) const;

  std::string ws_url_{};
  const SymbolTable* symbols_{nullptr};
  std::unordered_map<std::string, std::string> ticker_to_key_{};
  std::unordered_map<std::string, SymbolId> key_to_symbol_{};

  std::mutex mu_{};
  std::unordered_set<SymbolId> subscribed_{};

  std::atomic<bool> stop_{false};
  std::atomic<bool> connected_{false};
  int max_seconds_{0};
  std::thread thread_{};
};

}  // namespace algocraft
