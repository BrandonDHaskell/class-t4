#pragma once

#include <string_view>

namespace class_t4::core {

// A feature module (calendar, ...) as seen by the composition root. Lives in
// core so modules never depend on the server that registers them.
class module {
public:
  module() = default;
  module(const module &) = delete;
  module &operator=(const module &) = delete;
  module(module &&) = delete;
  module &operator=(module &&) = delete;
  virtual ~module() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;
};

} // namespace class_t4::core
