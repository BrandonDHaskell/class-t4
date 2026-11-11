#include "cli/cli.hpp"

#include "class_t4/core/result.hpp"

#include <cstddef>
#include <format>
#include <span>
#include <string_view>

namespace class_t4::server::cli {
namespace {

core::result<command> parse_serve(std::span<const std::string_view> args) {
  constexpr std::string_view config_flag = "--config";
  serve_command serve;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto arg = args[i];
    if (arg == config_flag) {
      if (i + 1 >= args.size()) {
        return core::make_error("serve: --config needs a path");
      }
      serve.config_path = args[++i];
    } else if (arg.starts_with("--config=")) {
      serve.config_path = arg.substr(config_flag.size() + 1);
    } else {
      return core::make_error(
          std::format("serve: unexpected argument '{}'", arg));
    }
  }
  if (serve.config_path.empty()) {
    return core::make_error("serve: --config <path> is required");
  }
  return serve;
}

} // namespace

core::result<command> parse(std::span<const std::string_view> args) {
  if (args.empty()) {
    return core::make_error("missing command");
  }
  const auto first = args.front();
  if (first == "serve") {
    return parse_serve(args.subspan(1));
  }
  if (args.size() == 1 && (first == "--version" || first == "version")) {
    return version_command{};
  }
  if (args.size() == 1 &&
      (first == "--help" || first == "-h" || first == "help")) {
    return help_command{};
  }
  return core::make_error(std::format("unknown command '{}'", first));
}

std::string_view usage_text() {
  return "usage: class-t4 <command>\n"
         "\n"
         "commands:\n"
         "  serve --config <path>  run the HTTPS API and the ops listener\n"
         "  --version              print version and commit\n"
         "  --help                 print this help\n";
}

} // namespace class_t4::server::cli
