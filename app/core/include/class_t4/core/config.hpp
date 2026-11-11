#pragma once

#include "class_t4/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace class_t4::core {

enum class log_level : std::uint8_t { trace, debug, info, warn, error };

struct https_config {
  std::string address = "0.0.0.0";
  std::uint16_t port = 8443;
  std::string cert_file;
  std::string key_file;
};

struct ops_config {
  std::string address = "0.0.0.0";
  std::uint16_t port = 9464;
};

// Every key is documented in docs/ops-contract.md and shown in
// config/class-t4.example.json. Only the TLS file paths are required.
struct config {
  https_config https;
  ops_config ops;
  unsigned threads = 2;
  log_level log = log_level::info;
};

// Parses and validates a JSON config document. Unknown keys are errors, so a
// typo fails at startup instead of being silently ignored.
result<config> parse_config(std::string_view json);

// Reads the file at path and parses it with parse_config.
result<config> load_config(const std::filesystem::path &path);

} // namespace class_t4::core
