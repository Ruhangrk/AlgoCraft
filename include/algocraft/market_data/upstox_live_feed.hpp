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
#include "algocraft/market_data/upstox_provider.hpp"

namespace algocraft {

// Upstox v3 market-data WebSocket (authorize → binary sub → protobuf LTPC).
class UpstoxLiveFeed final : public MarketDataFeed {
public:
  UpstoxLiveFeed(UpstoxConfig config, const SymbolTable* symbols,
                 std::unordered_map<std::string, std::string> ticker_to_key);

  ~UpstoxLiveFeed() override;

  void connect() override;
  void disconnect() override;
  void subscribe(SymbolId symbol_id) override;
  void unsubscribe(SymbolId symbol_id) override;

  // Optional: stop recv loop after N seconds (tests/smoke). 0 = until disconnect.
  void set_max_seconds(int seconds) { max_seconds_ = seconds; }

private:
  void run_loop();
  [[nodiscard]] std::string authorize_redirect_uri() const;
  [[nodiscard]] std::string instrument_key(SymbolId id) const;
  void handle_binary_frame(const std::string& frame);

  UpstoxConfig config_{};
  const SymbolTable* symbols_{nullptr};
  std::unordered_map<std::string, std::string> ticker_to_key_{};
  std::unordered_map<std::string, SymbolId> key_to_symbol_{};

  std::mutex mu_{};
  std::unordered_set<SymbolId> subscribed_{};
  std::vector<SymbolId> pending_sub_{};

  std::atomic<bool> stop_{false};
  std::atomic<bool> connected_{false};
  int max_seconds_{0};
  std::thread thread_{};
};

}  // namespace algocraft
