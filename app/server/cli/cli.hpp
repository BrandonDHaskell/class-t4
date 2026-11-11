#pragma once

#include "class_t4/core/result.hpp"

#include <filesystem>
#include <span>
#include <string_view>
#include <variant>

namespace class_t4::server::cli {

struct serve_command {
  std::filesystem::path config_path;
};
struct version_command {};
struct help_command {};

using command = std::variant<serve_command, version_command, help_command>;

// Parses the arguments after the program name. Errors describe the usage
// problem; the caller prints them with usage_text() and exits 2.
core::result<command> parse(std::span<const std::string_view> args);

std::string_view usage_text();

} // namespace class_t4::server::cli
