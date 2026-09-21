#include "algocraft/market_data/instrument_ingest.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <curl/curl.h>
#include <zlib.h>

namespace algocraft {
namespace {

size_t write_body(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

std::string http_get_bytes(std::string_view url) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }
  std::string body;
  curl_easy_setopt(curl, CURLOPT_URL, std::string{url}.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "algocraft/0.1");
  const CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(curl);
  if (rc != CURLE_OK) {
    throw std::runtime_error(std::string("instruments download: ") + curl_easy_strerror(rc));
  }
  if (status < 200 || status >= 300) {
    throw std::runtime_error("instruments download HTTP " + std::to_string(status));
  }
  return body;
}

std::string gunzip_bytes(std::string_view gz) {
  if (gz.size() < 2 || static_cast<unsigned char>(gz[0]) != 0x1f ||
      static_cast<unsigned char>(gz[1]) != 0x8b) {
    throw std::runtime_error("instruments: not gzip");
  }
  std::string input{gz};
  z_stream strm{};
  strm.next_in = reinterpret_cast<Bytef*>(input.data());
  strm.avail_in = static_cast<uInt>(input.size());
  if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK) {
    throw std::runtime_error("inflateInit2 failed");
  }
  std::string out;
  out.reserve(input.size() * 8);
  char buf[1 << 16];
  int rc = Z_OK;
  while (rc != Z_STREAM_END) {
    strm.next_out = reinterpret_cast<Bytef*>(buf);
    strm.avail_out = sizeof(buf);
    rc = inflate(&strm, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) {
      inflateEnd(&strm);
      throw std::runtime_error("inflate failed");
    }
    out.append(buf, sizeof(buf) - strm.avail_out);
  }
  inflateEnd(&strm);
  return out;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open " + path.string());
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

// Minimal RFC4180 CSV: returns fields for one logical line; advances `pos`.
bool next_csv_record(std::string_view text, std::size_t& pos, std::vector<std::string>& fields) {
  fields.clear();
  if (pos >= text.size()) {
    return false;
  }
  std::string field;
  bool in_quotes = false;
  for (;;) {
    if (pos >= text.size()) {
      fields.push_back(std::move(field));
      return true;
    }
    const char c = text[pos++];
    if (in_quotes) {
      if (c == '"') {
        if (pos < text.size() && text[pos] == '"') {
          field.push_back('"');
          ++pos;
        } else {
          in_quotes = false;
        }
      } else {
        field.push_back(c);
      }
      continue;
    }
    if (c == '"') {
      in_quotes = true;
      continue;
    }
    if (c == ',') {
      fields.push_back(std::move(field));
      field.clear();
      continue;
    }
    if (c == '\n') {
      if (!field.empty() && field.back() == '\r') {
        field.pop_back();
      }
      fields.push_back(std::move(field));
      return true;
    }
    if (c == '\r') {
      continue;
    }
    field.push_back(c);
  }
}

std::int64_t tick_rupees_to_paise(std::string_view tick) {
  if (tick.empty()) {
    return 5;
  }
  try {
    const double rupees = std::stod(std::string{tick});
    return static_cast<std::int64_t>(std::llround(rupees * 100.0));
  } catch (...) {
    return 5;
  }
}

std::int64_t parse_lot(std::string_view lot) {
  if (lot.empty()) {
    return 1;
  }
  try {
    return std::stoll(std::string{lot});
  } catch (...) {
    return 1;
  }
}

bool is_nse_eq_cash(std::string_view exchange, std::string_view instrument_type,
                    std::string_view instrument_key, std::string& isin_out) {
  if (exchange != "NSE_EQ" || instrument_type != "EQUITY") {
    return false;
  }
  constexpr std::string_view kPrefix = "NSE_EQ|";
  if (instrument_key.size() <= kPrefix.size() ||
      instrument_key.compare(0, kPrefix.size(), kPrefix) != 0) {
    return false;
  }
  isin_out = std::string(instrument_key.substr(kPrefix.size()));
  // Listed cash equities use INE… ISINs (exclude SDL / bonds / others on NSE_EQ).
  return isin_out.size() >= 3 && isin_out.compare(0, 3, "INE") == 0;
}

}  // namespace

std::string load_upstox_instruments_csv(std::string_view source) {
  const std::string src{source};
  if (src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0) {
    auto bytes = http_get_bytes(src);
    if (src.size() >= 3 && src.substr(src.size() - 3) == ".gz") {
      return gunzip_bytes(bytes);
    }
    return bytes;
  }
  const std::filesystem::path path{src};
  auto bytes = read_file(path);
  if (path.extension() == ".gz") {
    return gunzip_bytes(bytes);
  }
  return bytes;
}

InstrumentIngestStats parse_upstox_instruments_csv(std::string_view csv,
                                                   std::vector<InstrumentRow>& out) {
  InstrumentIngestStats stats{};
  std::size_t pos = 0;
  std::vector<std::string> fields;
  if (!next_csv_record(csv, pos, fields) || fields.empty()) {
    throw std::runtime_error("instruments csv: empty");
  }
  std::unordered_map<std::string, int> col;
  for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
    col[fields[static_cast<std::size_t>(i)]] = i;
  }
  const auto require = [&](const char* name) -> int {
    const auto it = col.find(name);
    if (it == col.end()) {
      throw std::runtime_error(std::string("instruments csv missing column: ") + name);
    }
    return it->second;
  };
  const int i_key = require("instrument_key");
  const int i_sym = require("tradingsymbol");
  const int i_name = require("name");
  const int i_tick = require("tick_size");
  const int i_lot = require("lot_size");
  const int i_type = require("instrument_type");
  const int i_exch = require("exchange");

  while (next_csv_record(csv, pos, fields)) {
    if (fields.size() < col.size()) {
      continue;
    }
    ++stats.rows_read;
    const auto& key = fields[static_cast<std::size_t>(i_key)];
    const auto& exch = fields[static_cast<std::size_t>(i_exch)];
    const auto& type = fields[static_cast<std::size_t>(i_type)];
    std::string isin;
    if (!is_nse_eq_cash(exch, type, key, isin)) {
      ++stats.rows_skipped;
      continue;
    }
    const auto& ticker = fields[static_cast<std::size_t>(i_sym)];
    if (ticker.empty()) {
      ++stats.rows_skipped;
      continue;
    }
    InstrumentRow row;
    row.ticker = ticker;
    row.name = fields[static_cast<std::size_t>(i_name)];
    row.isin = std::move(isin);
    row.exchange = "NSE";
    row.segment = "EQ";
    row.lot_size = parse_lot(fields[static_cast<std::size_t>(i_lot)]);
    row.tick_size_paise = tick_rupees_to_paise(fields[static_cast<std::size_t>(i_tick)]);
    row.instrument_key = key;
    row.active = true;
    out.push_back(std::move(row));
    ++stats.rows_kept;
  }
  return stats;
}

InstrumentIngestStats ingest_upstox_instruments(InstrumentRepository& repo,
                                                std::string_view source) {
  const auto csv = load_upstox_instruments_csv(source);
  std::vector<InstrumentRow> rows;
  rows.reserve(8192);
  auto stats = parse_upstox_instruments_csv(csv, rows);
  repo.mark_all_inactive();
  repo.upsert_many(rows);
  return stats;
}

}  // namespace algocraft
