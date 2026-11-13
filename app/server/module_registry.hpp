#pragma once

#include "class_t4/core/module.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace class_t4::server {

// Owns the feature modules wired in by main. Empty until the calendar
// module lands.
class module_registry {
public:
  void add(std::unique_ptr<core::module> module) {
    modules_.push_back(std::move(module));
  }

  [[nodiscard]] std::vector<std::string> names() const {
    std::vector<std::string> out;
    out.reserve(modules_.size());
    for (const auto &module : modules_) {
      out.emplace_back(module->name());
    }
    return out;
  }

private:
  std::vector<std::unique_ptr<core::module>> modules_;
};

} // namespace class_t4::server
