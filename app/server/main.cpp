// Composition root: parses the command line, loads config, wires modules and
// platform services, and runs the selected command.
#include "class_t4/core/build_info.hpp"
#include "class_t4/core/config.hpp"
#include "class_t4/platform/drogon/server.hpp"
#include "cli/cli.hpp"
#include "module_registry.hpp"

#include <cstdio>
#include <exception>
#include <iterator>
#include <print>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace class_t4;

constexpr int exit_ok = 0;
constexpr int exit_failure = 1;
constexpr int exit_usage = 2;

core::build_info this_build() {
  return {.version = CLASS_T4_VERSION, .commit = CLASS_T4_COMMIT};
}

int serve(const server::cli::serve_command &command) {
  auto config = core::load_config(command.config_path);
  if (!config) {
    std::println(stderr, "error: {}", config.error().message);
    return exit_failure;
  }

  const server::module_registry modules;

  const auto result = platform::drogon_server::run({
      .config = *std::move(config),
      .build = this_build(),
      .modules = modules.names(),
  });
  if (!result) {
    std::println(stderr, "error: {}", result.error().message);
    return exit_failure;
  }
  return exit_ok;
}

int run(std::span<char *> argv) {
  const std::vector<std::string_view> args(std::next(argv.begin()), argv.end());
  const auto command = server::cli::parse(args);
  if (!command) {
    std::print(stderr, "error: {}\n\n{}", command.error().message,
               server::cli::usage_text());
    return exit_usage;
  }

  struct visitor {
    int operator()(const server::cli::serve_command &c) const {
      return serve(c);
    }
    int operator()(const server::cli::version_command & /*c*/) const {
      const auto build = this_build();
      std::println("class-t4 {} ({})", build.version, build.commit);
      return exit_ok;
    }
    int operator()(const server::cli::help_command & /*c*/) const {
      std::print("{}", server::cli::usage_text());
      return exit_ok;
    }
  };
  return std::visit(visitor{}, *command);
}

} // namespace

int main(int argc, char **argv) {
  try {
    return run(std::span{argv, static_cast<std::size_t>(argc)});
  } catch (const std::exception &e) {
    // Nothing useful is left to do if writing the error fails.
    static_cast<void>(std::fputs(e.what(), stderr));
    return exit_failure;
  }
}
