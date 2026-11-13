#include "class_t4/core/health.hpp"

#include "class_t4/core/build_info.hpp"

#include <json/value.h>
#include <json/writer.h>

#include <string>

namespace class_t4::core {

std::string health_body(const build_info &info) {
  Json::Value body{Json::objectValue};
  body["status"] = "ok";
  body["version"] = info.version;

  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  return Json::writeString(writer, body);
}

} // namespace class_t4::core
