#pragma once

#include <expected>
#include <string>
#include <utility>

namespace class_t4::core {

// Failure detail for operations that can fail for reasons the caller should
// report, not crash on (bad config, unreadable files).
struct error {
  std::string message;
};

template <typename T> using result = std::expected<T, error>;

inline std::unexpected<error> make_error(std::string message) {
  return std::unexpected<error>{error{std::move(message)}};
}

} // namespace class_t4::core
