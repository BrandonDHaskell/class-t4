#include "class_t4/platform/drogon/server.hpp"

#include "class_t4/core/config.hpp"
#include "class_t4/core/health.hpp"
#include "class_t4/core/metrics.hpp"
#include "class_t4/core/result.hpp"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <trantor/net/InetAddress.h>
#include <trantor/utils/Logger.h>

#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace class_t4::platform::drogon_server {
namespace {

constexpr std::string_view prometheus_content_type =
    "text/plain; version=0.0.4; charset=utf-8";

trantor::Logger::LogLevel to_drogon(core::log_level level) {
  switch (level) {
  case core::log_level::trace:
    return trantor::Logger::kTrace;
  case core::log_level::debug:
    return trantor::Logger::kDebug;
  case core::log_level::info:
    return trantor::Logger::kInfo;
  case core::log_level::warn:
    return trantor::Logger::kWarn;
  case core::log_level::error:
    return trantor::Logger::kError;
  }
  return trantor::Logger::kInfo;
}

core::result<void> check_readable(std::string_view what,
                                  const std::string &path) {
  if (!std::ifstream{path}) {
    return core::make_error(std::format("{} {} is not readable", what, path));
  }
  return {};
}

// Directories Drogon insists on having. Under the temp directory, which is
// private to the service under systemd's PrivateTmp.
struct scratch_dirs {
  std::filesystem::path www;
  std::filesystem::path uploads;
};

core::result<scratch_dirs> make_scratch_dirs() {
  const auto base = std::filesystem::temp_directory_path() / "class-t4";
  scratch_dirs dirs{.www = base / "www", .uploads = base / "uploads"};
  std::error_code ec;
  std::filesystem::create_directories(dirs.www, ec);
  if (ec) {
    return core::make_error(
        std::format("cannot create {}: {}", dirs.www.string(), ec.message()));
  }
  return dirs;
}

std::string join(const std::vector<std::string> &names) {
  if (names.empty()) {
    return "none";
  }
  std::string out;
  for (const auto &name : names) {
    if (!out.empty()) {
      out += ", ";
    }
    out += name;
  }
  return out;
}

void register_routes(const server_options &options) {
  auto health = core::health_body(options.build);
  drogon::app().registerHandler(
      "/health",
      [body = std::move(health)](
          const drogon::HttpRequestPtr & /*request*/,
          std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
        const auto response = drogon::HttpResponse::newHttpResponse();
        response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        response->setBody(body);
        callback(response);
      },
      {drogon::Get});

  // Routes are global in Drogon, so /metrics checks which listener the
  // request arrived on. It must never be served on the LAN-facing API.
  const std::array families{core::build_info_metric(options.build)};
  drogon::app().registerHandler(
      "/metrics",
      [body = core::render_prometheus(families),
       ops_port = options.config.ops.port](
          const drogon::HttpRequestPtr &request,
          std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
        if (request->getLocalAddr().toPort() != ops_port) {
          callback(drogon::HttpResponse::newNotFoundResponse(request));
          return;
        }
        const auto response = drogon::HttpResponse::newHttpResponse();
        response->setContentTypeString(prometheus_content_type);
        response->setBody(body);
        callback(response);
      },
      {drogon::Get});
}

} // namespace

core::result<void> run(const server_options &options) {
  const auto &config = options.config;
  if (auto r = check_readable("TLS certificate", config.https.cert_file); !r) {
    return r;
  }
  if (auto r = check_readable("TLS key", config.https.key_file); !r) {
    return r;
  }
  const auto scratch = make_scratch_dirs();
  if (!scratch) {
    return std::unexpected{scratch.error()};
  }

  auto &app = drogon::app();
  app.setLogLevel(to_drogon(config.log))
      .setThreadNum(config.threads)
      .enableServerHeader(false)
      // Drogon serves static files from the working directory by default.
      // This service has none: allow no file types and point the document
      // root at an empty private directory, so the working directory is never
      // consulted (not even to answer 403 for files that exist).
      .setFileTypes({})
      .setDocumentRoot(scratch->www.string())
      // No uploads are accepted, but Drogon still creates its upload tree at
      // startup.
      .setUploadPath(scratch->uploads.string())
      .addListener(config.https.address, config.https.port, true,
                   config.https.cert_file, config.https.key_file)
      .addListener(config.ops.address, config.ops.port, false);

  const auto shutdown = [](std::string_view signal) {
    LOG_INFO << signal << " received, shutting down";
    drogon::app().quit();
  };
  app.setTermSignalHandler([shutdown] { shutdown("SIGTERM"); });
  app.setIntSignalHandler([shutdown] { shutdown("SIGINT"); });

  register_routes(options);

  LOG_INFO << "class-t4 " << options.build.version << " ("
           << options.build.commit << ") starting: https "
           << config.https.address << ":" << config.https.port << ", ops "
           << config.ops.address << ":" << config.ops.port
           << ", modules: " << join(options.modules);
  app.run();
  LOG_INFO << "class-t4 stopped";
  return {};
}

} // namespace class_t4::platform::drogon_server
