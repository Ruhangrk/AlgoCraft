#include "algocraft/market_data/virtual_live_feed.hpp"

#include <curl/curl.h>
#include <curl/websockets.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>

#include <spdlog/spdlog.h>

namespace algocraft {
namespace {

std::int64_t rupees_to_paise(double px) {
  return static_cast<std::int64_t>(std::llround(px * 100.0));
}

// Minimal JSON field extractors (same style as Upstox live feed).
std::optional<std::string> json_str(std::string_view body, std::string_view key) {
  const auto needle = std::string("\"") + std::string(key) + "\"";
  auto pos = body.find(needle);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  pos = body.find(':', pos);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  pos = body.find('"', pos);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto end = body.find('"', pos + 1);
  if (end == std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(body.substr(pos + 1, end - pos - 1));
}

std::optional<double> json_num(std::string_view body, std::string_view key) {
  const auto needle = std::string("\"") + std::string(key) + "\"";
  auto pos = body.find(needle);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  pos = body.find(':', pos);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  ++pos;
  while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) {
    ++pos;
  }
  char* end = nullptr;
  const auto v = std::strtod(body.data() + pos, &end);
  if (end == body.data() + pos) {
    return std::nullopt;
  }
  return v;
}

void ws_wait(CURL* curl, int timeout_ms) {
  curl_socket_t sock = CURL_SOCKET_BAD;
  if (curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sock) != CURLE_OK || sock == CURL_SOCKET_BAD) {
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    return;
  }
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(sock, &fds);
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  select(static_cast<int>(sock) + 1, &fds, nullptr, nullptr, &tv);
}

}  // namespace

VirtualLiveFeed::VirtualLiveFeed(std::string ws_url, const SymbolTable* symbols,
                                 std::unordered_map<std::string, std::string> ticker_to_key)
    : ws_url_(std::move(ws_url)),
      symbols_(symbols),
      ticker_to_key_(std::move(ticker_to_key)) {
  for (const auto& [ticker, key] : ticker_to_key_) {
    if (symbols_ == nullptr) {
      continue;
    }
    if (const auto id = symbols_->find(ticker)) {
      key_to_symbol_[key] = *id;
    }
  }
}

VirtualLiveFeed::~VirtualLiveFeed() { disconnect(); }

std::string VirtualLiveFeed::instrument_key(SymbolId id) const {
  if (symbols_ == nullptr || !symbols_->contains(id)) {
    return {};
  }
  const auto ticker = std::string{symbols_->symbol(id).ticker};
  const auto it = ticker_to_key_.find(ticker);
  if (it != ticker_to_key_.end()) {
    return it->second;
  }
  return "NSE_EQ|" + ticker;
}

void VirtualLiveFeed::connect() {
  if (connected_) {
    return;
  }
  stop_ = false;
  connected_ = true;
  thread_ = std::thread([this] { run_loop(); });
}

void VirtualLiveFeed::disconnect() {
  stop_ = true;
  if (thread_.joinable()) {
    thread_.join();
  }
  connected_ = false;
}

void VirtualLiveFeed::subscribe(SymbolId symbol_id) {
  std::lock_guard lock(mu_);
  subscribed_.insert(symbol_id);
  const auto key = instrument_key(symbol_id);
  if (!key.empty()) {
    key_to_symbol_[key] = symbol_id;
  }
}

void VirtualLiveFeed::unsubscribe(SymbolId symbol_id) {
  std::lock_guard lock(mu_);
  subscribed_.erase(symbol_id);
}

void VirtualLiveFeed::run_loop() {
  try {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
      throw std::runtime_error("curl_easy_init failed");
    }
    curl_easy_setopt(curl, CURLOPT_URL, ws_url_.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "algocraft-virtual/0.1");

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
      curl_easy_cleanup(curl);
      throw std::runtime_error(std::string("virtual ws connect: ") + curl_easy_strerror(rc));
    }

    // Subscribe JSON — same shape as Upstox sub (text frame for virtual server).
    {
      std::ostringstream sub;
      sub << "{\"guid\":\"algocraft\",\"method\":\"sub\",\"data\":{\"mode\":\"ltpc\","
             "\"instrumentKeys\":[";
      bool first = true;
      std::lock_guard lock(mu_);
      for (const auto id : subscribed_) {
        const auto key = instrument_key(id);
        if (key.empty()) {
          continue;
        }
        if (!first) {
          sub << ',';
        }
        first = false;
        sub << '"' << key << '"';
        key_to_symbol_[key] = id;
      }
      sub << "]}}";
      const auto json = sub.str();
      std::size_t sent = 0;
      curl_ws_send(curl, json.data(), json.size(), &sent, 0, CURLWS_TEXT);
    }

    const auto deadline = max_seconds_ > 0
                              ? std::chrono::steady_clock::now() + std::chrono::seconds(max_seconds_)
                              : std::chrono::steady_clock::time_point::max();
    while (!stop_ && std::chrono::steady_clock::now() < deadline) {
      char chunk[8192];
      std::size_t nread = 0;
      const struct curl_ws_frame* meta = nullptr;
      rc = curl_ws_recv(curl, chunk, sizeof(chunk), &nread, &meta);
      if (rc == CURLE_AGAIN) {
        ws_wait(curl, 200);
        continue;
      }
      if (rc != CURLE_OK) {
        spdlog::warn("virtual ws recv: {}", curl_easy_strerror(rc));
        break;
      }
      if (meta == nullptr || nread == 0) {
        continue;
      }
      if (meta->flags & CURLWS_CLOSE) {
        break;
      }
      const std::string_view frame(chunk, nread);
      const auto key = json_str(frame, "instrumentKey");
      const auto ltp = json_num(frame, "ltp");
      if (!key || !ltp) {
        continue;
      }
      SymbolId id = 0;
      {
        std::lock_guard lock(mu_);
        const auto it = key_to_symbol_.find(*key);
        if (it == key_to_symbol_.end() || !subscribed_.contains(it->second)) {
          continue;
        }
        id = it->second;
      }
      auto ts = Timestamp::now();
      if (auto ltt = json_num(frame, "ltt")) {
        ts = Timestamp::from_nanos(static_cast<std::int64_t>(*ltt) * 1'000'000LL);
      }
      emit_tick(id, Price::from_paise(rupees_to_paise(*ltp)), ts);
    }
    curl_easy_cleanup(curl);
  } catch (const std::exception& e) {
    spdlog::error("virtual live feed: {}", e.what());
  }
  connected_ = false;
}

}  // namespace algocraft
