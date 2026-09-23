#include "algocraft/market_data/upstox_live_feed.hpp"

#include <curl/curl.h>
#include <curl/websockets.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

#include <spdlog/spdlog.h>

namespace algocraft {
namespace {

struct ProtoBuf {
  const std::uint8_t* p{nullptr};
  const std::uint8_t* end{nullptr};
};

bool proto_varint(ProtoBuf& b, std::uint64_t& out) {
  out = 0;
  int shift = 0;
  while (b.p < b.end && shift <= 63) {
    const auto byte = *b.p++;
    out |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) {
      return true;
    }
    shift += 7;
  }
  return false;
}

bool proto_fixed64(ProtoBuf& b, std::uint64_t& out) {
  if (b.end - b.p < 8) {
    return false;
  }
  std::memcpy(&out, b.p, 8);
  b.p += 8;
  return true;
}

bool proto_len(ProtoBuf& b, ProtoBuf& inner) {
  std::uint64_t len = 0;
  if (!proto_varint(b, len)) {
    return false;
  }
  if (static_cast<std::uint64_t>(b.end - b.p) < len) {
    return false;
  }
  inner.p = b.p;
  inner.end = b.p + static_cast<std::ptrdiff_t>(len);
  b.p = inner.end;
  return true;
}

bool proto_skip(ProtoBuf& b, std::uint32_t wire) {
  std::uint64_t tmp = 0;
  switch (wire) {
    case 0:
      return proto_varint(b, tmp);
    case 1:
      return proto_fixed64(b, tmp);
    case 2: {
      ProtoBuf inner;
      return proto_len(b, inner);
    }
    case 5:
      if (b.end - b.p < 4) {
        return false;
      }
      b.p += 4;
      return true;
    default:
      return false;
  }
}

bool proto_key(ProtoBuf& b, std::uint32_t& field, std::uint32_t& wire) {
  std::uint64_t tag = 0;
  if (!proto_varint(b, tag)) {
    return false;
  }
  field = static_cast<std::uint32_t>(tag >> 3);
  wire = static_cast<std::uint32_t>(tag & 7);
  return field != 0;
}

double bits_to_double(std::uint64_t bits) {
  double value = 0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

struct Ltpc {
  double ltp{0};
  std::int64_t ltt{0};
  bool have_ltp{false};
};

Ltpc decode_ltpc(ProtoBuf b) {
  Ltpc out{};
  while (b.p < b.end) {
    std::uint32_t field = 0;
    std::uint32_t wire = 0;
    if (!proto_key(b, field, wire)) {
      break;
    }
    if (field == 1 && wire == 1) {
      std::uint64_t bits = 0;
      if (!proto_fixed64(b, bits)) {
        break;
      }
      out.ltp = bits_to_double(bits);
      out.have_ltp = true;
    } else if (field == 2 && wire == 0) {
      std::uint64_t v = 0;
      if (!proto_varint(b, v)) {
        break;
      }
      out.ltt = static_cast<std::int64_t>(v);
    } else if (!proto_skip(b, wire)) {
      break;
    }
  }
  return out;
}

Ltpc decode_ltpc_from_full(ProtoBuf b) {
  while (b.p < b.end) {
    std::uint32_t field = 0;
    std::uint32_t wire = 0;
    if (!proto_key(b, field, wire)) {
      break;
    }
    if (field == 1 && wire == 2) {
      ProtoBuf inner;
      if (!proto_len(b, inner)) {
        break;
      }
      return decode_ltpc(inner);
    }
    if (!proto_skip(b, wire)) {
      break;
    }
  }
  return {};
}

Ltpc decode_feed(ProtoBuf b) {
  Ltpc ltpc{};
  while (b.p < b.end) {
    std::uint32_t field = 0;
    std::uint32_t wire = 0;
    if (!proto_key(b, field, wire)) {
      break;
    }
    if (field == 1 && wire == 2) {
      ProtoBuf inner;
      if (!proto_len(b, inner)) {
        break;
      }
      ltpc = decode_ltpc(inner);
    } else if ((field == 2 || field == 3) && wire == 2) {
      ProtoBuf inner;
      if (!proto_len(b, inner)) {
        break;
      }
      ProtoBuf walk = inner;
      while (walk.p < walk.end) {
        std::uint32_t f2 = 0;
        std::uint32_t w2 = 0;
        if (!proto_key(walk, f2, w2)) {
          break;
        }
        if ((f2 == 1 || f2 == 2) && w2 == 2) {
          ProtoBuf nested;
          if (!proto_len(walk, nested)) {
            break;
          }
          const auto found = decode_ltpc_from_full(nested);
          if (found.have_ltp) {
            ltpc = found;
          }
        } else if (!proto_skip(walk, w2)) {
          break;
        }
      }
    } else if (!proto_skip(b, wire)) {
      break;
    }
  }
  return ltpc;
}

struct FeedTick {
  std::string key;
  Ltpc ltpc{};
};

std::vector<FeedTick> decode_feed_response(const std::uint8_t* data, std::size_t size) {
  ProtoBuf b{data, data + size};
  std::vector<FeedTick> out;
  while (b.p < b.end) {
    std::uint32_t field = 0;
    std::uint32_t wire = 0;
    if (!proto_key(b, field, wire)) {
      break;
    }
    if (field == 2 && wire == 2) {
      ProtoBuf entry;
      if (!proto_len(b, entry)) {
        break;
      }
      FeedTick tick{};
      while (entry.p < entry.end) {
        std::uint32_t f2 = 0;
        std::uint32_t w2 = 0;
        if (!proto_key(entry, f2, w2)) {
          break;
        }
        if (f2 == 1 && w2 == 2) {
          ProtoBuf kbuf;
          if (!proto_len(entry, kbuf)) {
            break;
          }
          tick.key.assign(reinterpret_cast<const char*>(kbuf.p),
                          static_cast<std::size_t>(kbuf.end - kbuf.p));
        } else if (f2 == 2 && w2 == 2) {
          ProtoBuf fbuf;
          if (!proto_len(entry, fbuf)) {
            break;
          }
          tick.ltpc = decode_feed(fbuf);
        } else if (!proto_skip(entry, w2)) {
          break;
        }
      }
      if (!tick.key.empty() && tick.ltpc.have_ltp) {
        out.push_back(std::move(tick));
      }
    } else if (!proto_skip(b, wire)) {
      break;
    }
  }
  return out;
}

std::size_t write_body(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

std::optional<std::string> json_string_field(std::string_view body, std::string_view key) {
  const auto needle = std::string("\"") + std::string(key) + "\"";
  const auto pos = body.find(needle);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto colon = body.find(':', pos);
  const auto q1 = body.find('"', colon);
  if (q1 == std::string_view::npos) {
    return std::nullopt;
  }
  const auto q2 = body.find('"', q1 + 1);
  if (q2 == std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(body.substr(q1 + 1, q2 - q1 - 1));
}

std::string json_escape(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(static_cast<char>(c));
  }
  return out;
}

void ws_wait_readable(CURL* curl, long timeout_ms) {
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

std::int64_t rupees_to_paise(double px) {
  return static_cast<std::int64_t>(std::llround(px * 100.0));
}

}  // namespace

UpstoxLiveFeed::UpstoxLiveFeed(UpstoxConfig config, const SymbolTable* symbols,
                               std::unordered_map<std::string, std::string> ticker_to_key)
    : config_(std::move(config)),
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

UpstoxLiveFeed::~UpstoxLiveFeed() { disconnect(); }

std::string UpstoxLiveFeed::instrument_key(SymbolId id) const {
  if (symbols_ == nullptr || !symbols_->contains(id)) {
    return {};
  }
  const auto ticker = std::string{symbols_->symbol(id).ticker};
  const auto it = ticker_to_key_.find(ticker);
  return it == ticker_to_key_.end() ? std::string{} : it->second;
}

std::string UpstoxLiveFeed::authorize_redirect_uri() const {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }
  auto base = config_.history_base_url;
  while (!base.empty() && base.back() == '/') {
    base.pop_back();
  }
  const auto url = base + "/feed/market-data-feed/authorize";
  std::string body;
  const auto auth = "Authorization: Bearer " + config_.access_token;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, auth.c_str());
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "algocraft/0.1");
  const auto rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  if (rc != CURLE_OK) {
    throw std::runtime_error(std::string("upstox authorize: ") + curl_easy_strerror(rc));
  }
  if (status < 200 || status >= 300) {
    throw std::runtime_error("upstox authorize HTTP " + std::to_string(status));
  }
  auto uri = json_string_field(body, "authorized_redirect_uri");
  if (!uri || uri->empty()) {
    throw std::runtime_error("authorize missing authorized_redirect_uri");
  }
  return *uri;
}

void UpstoxLiveFeed::connect() {
  if (connected_) {
    return;
  }
  stop_ = false;
  connected_ = true;
  thread_ = std::thread([this] { run_loop(); });
}

void UpstoxLiveFeed::disconnect() {
  stop_ = true;
  if (thread_.joinable()) {
    thread_.join();
  }
  connected_ = false;
}

void UpstoxLiveFeed::subscribe(SymbolId symbol_id) {
  std::lock_guard lock(mu_);
  subscribed_.insert(symbol_id);
  pending_sub_.push_back(symbol_id);
  const auto key = instrument_key(symbol_id);
  if (!key.empty()) {
    key_to_symbol_[key] = symbol_id;
  }
}

void UpstoxLiveFeed::unsubscribe(SymbolId symbol_id) {
  std::lock_guard lock(mu_);
  subscribed_.erase(symbol_id);
}

void UpstoxLiveFeed::handle_binary_frame(const std::string& frame) {
  const auto ticks = decode_feed_response(reinterpret_cast<const std::uint8_t*>(frame.data()),
                                          frame.size());
  for (const auto& t : ticks) {
    SymbolId id = 0;
    {
      std::lock_guard lock(mu_);
      const auto it = key_to_symbol_.find(t.key);
      if (it == key_to_symbol_.end()) {
        continue;
      }
      id = it->second;
      if (!subscribed_.contains(id)) {
        continue;
      }
    }
    auto ts = Timestamp::now();
    if (t.ltpc.ltt > 0) {
      // ltt is typically epoch ms.
      ts = Timestamp::from_nanos(t.ltpc.ltt * 1'000'000LL);
    }
    emit_tick(id, Price::from_paise(rupees_to_paise(t.ltpc.ltp)), ts);
  }
}

void UpstoxLiveFeed::run_loop() {
  try {
    const auto ws_url = authorize_redirect_uri();
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
      throw std::runtime_error("curl_easy_init failed");
    }
    const auto auth = "Authorization: Bearer " + config_.access_token;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, auth.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, ws_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "algocraft/0.1");

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);
      throw std::runtime_error(std::string("WebSocket connect failed: ") + curl_easy_strerror(rc));
    }

    auto send_sub = [&](const std::vector<std::string>& keys) {
      if (keys.empty()) {
        return;
      }
      std::ostringstream sub;
      sub << "{\"guid\":\"algocraft\",\"method\":\"sub\",\"data\":{\"mode\":\"ltpc\","
             "\"instrumentKeys\":[";
      for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) {
          sub << ',';
        }
        sub << '"' << json_escape(keys[i]) << '"';
      }
      sub << "]}}";
      const auto json = sub.str();
      std::size_t sent = 0;
      rc = curl_ws_send(curl, json.data(), json.size(), &sent, 0, CURLWS_BINARY);
      if (rc != CURLE_OK) {
        throw std::runtime_error(std::string("WebSocket subscribe failed: ") +
                                 curl_easy_strerror(rc));
      }
    };

    {
      std::vector<std::string> keys;
      std::lock_guard lock(mu_);
      for (const auto id : subscribed_) {
        auto key = instrument_key(id);
        if (!key.empty()) {
          keys.push_back(std::move(key));
        }
      }
      pending_sub_.clear();
      send_sub(keys);
    }

    const auto deadline = max_seconds_ > 0
                              ? std::chrono::steady_clock::now() + std::chrono::seconds(max_seconds_)
                              : std::chrono::steady_clock::time_point::max();
    std::string frame;
    while (!stop_ && std::chrono::steady_clock::now() < deadline) {
      {
        std::vector<std::string> keys;
        {
          std::lock_guard lock(mu_);
          for (const auto id : pending_sub_) {
            auto key = instrument_key(id);
            if (!key.empty()) {
              keys.push_back(std::move(key));
            }
          }
          pending_sub_.clear();
        }
        if (!keys.empty()) {
          send_sub(keys);
        }
      }

      char chunk[16384];
      std::size_t nread = 0;
      const struct curl_ws_frame* meta = nullptr;
      rc = curl_ws_recv(curl, chunk, sizeof(chunk), &nread, &meta);
      if (rc == CURLE_AGAIN) {
        ws_wait_readable(curl, 200);
        continue;
      }
      if (rc != CURLE_OK) {
        spdlog::warn("upstox ws recv: {}", curl_easy_strerror(rc));
        break;
      }
      if (meta == nullptr) {
        continue;
      }
      if (meta->flags & CURLWS_CLOSE) {
        break;
      }
      if (meta->flags & CURLWS_PING) {
        std::size_t pong_sent = 0;
        curl_ws_send(curl, chunk, nread, &pong_sent, 0, CURLWS_PONG);
        continue;
      }
      if (meta->offset == 0) {
        frame.clear();
      }
      frame.append(chunk, nread);
      if (meta->bytesleft > 0) {
        continue;
      }
      if (meta->flags & CURLWS_BINARY) {
        handle_binary_frame(frame);
      }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
  } catch (const std::exception& e) {
    spdlog::error("upstox live feed: {}", e.what());
  }
  connected_ = false;
}

}  // namespace algocraft
