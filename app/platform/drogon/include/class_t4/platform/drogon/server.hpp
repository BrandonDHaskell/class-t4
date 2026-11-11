#pragma once

#include "class_t4/core/build_info.hpp"
#include "class_t4/core/config.hpp"
#include "class_t4/core/result.hpp"

#include <string>
#include <vector>

namespace class_t4::platform::drogon_server {

struct server_options {
  core::config config;
  core::build_info build;
  // Names of the registered modules, logged at startup.
  std::vector<std::string> modules;
};

// Starts the HTTPS listener and the plain-HTTP ops listener and blocks until
// SIGTERM or SIGINT, then drains and returns. Fails before listening if the
// TLS files are unreadable.
core::result<void> run(const server_options &options);

} // namespace class_t4::platform::drogon_server
