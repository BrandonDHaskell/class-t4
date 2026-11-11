#include "class_t4/core/config.hpp"

#include "class_t4/core/result.hpp"

#include <arpa/inet.h>
#include <json/reader.h>
#include <json/value.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace class_t4::core {
namespace {

std::unexpected<error> config_error(std::string_view key,
                                    std::string_view problem) {
  return make_error(std::format("config: {}: {}", key, problem));
}

result<void>
reject_unknown_keys(const Json::Value &object, std::string_view where,
                    std::initializer_list<std::string_view> known) {
  for (const auto &key : object.getMemberNames()) {
    bool found = false;
    for (const auto candidate : known) {
      found = found || key == candidate;
    }
    if (!found) {
      const auto path = where.empty() ? key : std::format("{}.{}", where, key);
      return config_error(path, "unknown key");
    }
  }
  return {};
}

result<const Json::Value *> object_member(const Json::Value &parent,
                                          const char *key) {
  const Json::Value *value =
      parent.find(key, key + std::char_traits<char>::length(key));
  if (value != nullptr && !value->isObject()) {
    return config_error(key, "must be an object");
  }
  return value;
}

result<void> read_string(const Json::Value &object, const char *key,
                         std::string_view path, std::string &out) {
  const Json::Value &value = object[key];
  if (value.isNull()) {
    return {};
  }
  if (!value.isString() || value.asString().empty()) {
    return config_error(path, "must be a non-empty string");
  }
  out = value.asString();
  return {};
}

result<void> read_port(const Json::Value &object, const char *key,
                       std::string_view path, std::uint16_t &out) {
  const Json::Value &value = object[key];
  if (value.isNull()) {
    return {};
  }
  if (!value.isInt() || value.asInt() < 1 ||
      value.asInt() > std::numeric_limits<std::uint16_t>::max()) {
    return config_error(path, "must be an integer between 1 and 65535");
  }
  out = static_cast<std::uint16_t>(value.asInt());
  return {};
}

result<void> check_address(std::string_view path, const std::string &address) {
  std::array<unsigned char, sizeof(in6_addr)> buffer{};
  if (inet_pton(AF_INET, address.c_str(), buffer.data()) == 1 ||
      inet_pton(AF_INET6, address.c_str(), buffer.data()) == 1) {
    return {};
  }
  return config_error(path, "must be an IPv4 or IPv6 address literal");
}

std::optional<log_level> parse_log_level(std::string_view text) {
  if (text == "trace") {
    return log_level::trace;
  }
  if (text == "debug") {
    return log_level::debug;
  }
  if (text == "info") {
    return log_level::info;
  }
  if (text == "warn") {
    return log_level::warn;
  }
  if (text == "error") {
    return log_level::error;
  }
  return std::nullopt;
}

result<void> read_https(const Json::Value &root, https_config &out) {
  const auto section = object_member(root, "https");
  if (!section) {
    return std::unexpected{section.error()};
  }
  if (*section == nullptr) {
    return config_error("https", "is required");
  }
  const Json::Value &https = **section;
  if (auto r = reject_unknown_keys(
          https, "https", {"address", "port", "cert_file", "key_file"});
      !r) {
    return r;
  }
  if (auto r = read_string(https, "address", "https.address", out.address);
      !r) {
    return r;
  }
  if (auto r = read_port(https, "port", "https.port", out.port); !r) {
    return r;
  }
  if (auto r =
          read_string(https, "cert_file", "https.cert_file", out.cert_file);
      !r) {
    return r;
  }
  if (auto r = read_string(https, "key_file", "https.key_file", out.key_file);
      !r) {
    return r;
  }
  if (out.cert_file.empty()) {
    return config_error("https.cert_file", "is required");
  }
  if (out.key_file.empty()) {
    return config_error("https.key_file", "is required");
  }
  return check_address("https.address", out.address);
}

result<void> read_ops(const Json::Value &root, ops_config &out) {
  const auto section = object_member(root, "ops");
  if (!section) {
    return std::unexpected{section.error()};
  }
  if (*section == nullptr) {
    return {};
  }
  const Json::Value &ops = **section;
  if (auto r = reject_unknown_keys(ops, "ops", {"address", "port"}); !r) {
    return r;
  }
  if (auto r = read_string(ops, "address", "ops.address", out.address); !r) {
    return r;
  }
  if (auto r = read_port(ops, "port", "ops.port", out.port); !r) {
    return r;
  }
  return check_address("ops.address", out.address);
}

// Upper bound guards against typos (e.g. 400 instead of 4) on a 4-core host.
constexpr unsigned max_threads = 64;

result<void> read_top_level(const Json::Value &root, config &out) {
  if (const Json::Value &threads = root["threads"]; !threads.isNull()) {
    if (!threads.isUInt() || threads.asUInt() < 1 ||
        threads.asUInt() > max_threads) {
      return config_error(
          "threads",
          std::format("must be an integer between 1 and {}", max_threads));
    }
    out.threads = threads.asUInt();
  }
  if (const Json::Value &level = root["log_level"]; !level.isNull()) {
    const auto parsed =
        level.isString() ? parse_log_level(level.asString()) : std::nullopt;
    if (!parsed) {
      return config_error("log_level",
                          "must be one of trace, debug, info, warn, error");
    }
    out.log = *parsed;
  }
  return {};
}

} // namespace

result<config> parse_config(std::string_view json) {
  Json::CharReaderBuilder builder;
  Json::CharReaderBuilder::strictMode(&builder.settings_);

  // Config files are tiny; copying into a stream keeps the parse bounds-safe.
  std::istringstream stream{std::string{json}};
  Json::Value root;
  std::string errors;
  if (!Json::parseFromStream(builder, stream, &root, &errors)) {
    return make_error(std::format("config: invalid JSON: {}", errors));
  }
  if (!root.isObject()) {
    return make_error("config: top level must be a JSON object");
  }
  if (auto r = reject_unknown_keys(root, "",
                                   {"https", "ops", "threads", "log_level"});
      !r) {
    return std::unexpected{r.error()};
  }

  config out;
  if (auto r = read_https(root, out.https); !r) {
    return std::unexpected{r.error()};
  }
  if (auto r = read_ops(root, out.ops); !r) {
    return std::unexpected{r.error()};
  }
  if (auto r = read_top_level(root, out); !r) {
    return std::unexpected{r.error()};
  }
  if (out.https.port == out.ops.port) {
    return config_error("ops.port", "must differ from https.port");
  }
  return out;
}

result<config> load_config(const std::filesystem::path &path) {
  std::ifstream file{path, std::ios::binary};
  if (!file) {
    return make_error(std::format("config: cannot open {}", path.string()));
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  if (file.bad()) {
    return make_error(std::format("config: cannot read {}", path.string()));
  }
  return parse_config(contents.str());
}

} // namespace class_t4::core
