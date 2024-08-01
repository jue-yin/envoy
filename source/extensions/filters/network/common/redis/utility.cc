#include "source/extensions/filters/network/common/redis/utility.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Utility {

AuthRequest::AuthRequest(const std::string& password) {
  std::vector<RespValue> values(2);
  values[0].type(RespType::BulkString);
  values[0].asString() = "auth";
  values[1].type(RespType::BulkString);
  values[1].asString() = password;
  type(RespType::Array);
  asArray().swap(values);
}

AuthRequest::AuthRequest(const std::string& username, const std::string& password) {
  std::vector<RespValue> values(3);
  values[0].type(RespType::BulkString);
  values[0].asString() = "auth";
  values[1].type(RespType::BulkString);
  values[1].asString() = username;
  values[2].type(RespType::BulkString);
  values[2].asString() = password;
  type(RespType::Array);
  asArray().swap(values);
}

RespValuePtr makeError(const std::string& error) {
  Common::Redis::RespValuePtr response(new RespValue());
  response->type(Common::Redis::RespType::Error);
  response->asString() = error;
  return response;
}
#if defined(HIGRESS)
std::string makeRawError(const std::string& error) {
  std::string result;
  result.append(fmt::format("-{}\r\n", error));
  return result;
}

std::string makeRawRequest(const std::string& command, std::vector<std::string_view> params) {
  std::string result;
  result.append(fmt::format("*{}\r\n", 1 + params.size()));
  result.append(fmt::format("${}\r\n{}\r\n", command.size(), command));
  for (auto& param : params) {
    result.append(fmt::format("${}\r\n{}\r\n", param.size(), param));
  }
  return result;
}

std::string makeRawAuthRequest(const std::string& username, const std::string& password) {
  return makeRawRequest("AUTH", {username, password});
}

std::string makeRawAuthRequest(const std::string& password) {
  return makeRawRequest("AUTH", {password});
}

std::string_view makeRawReadOnlyRequest() {
  const std::string readonly{"readonly"};
  static const std::string readonly_request =
      fmt::format("${}\r\n{}\r\n", readonly.size(), readonly);
  return readonly_request;
}

std::string makeSelectRequest(const std::string& index) {
  return makeRawRequest("SELECT", {index});
}

#endif
ReadOnlyRequest::ReadOnlyRequest() {
  std::vector<RespValue> values(1);
  values[0].type(RespType::BulkString);
  values[0].asString() = "readonly";
  type(RespType::Array);
  asArray().swap(values);
}

const ReadOnlyRequest& ReadOnlyRequest::instance() {
  static const ReadOnlyRequest* instance = new ReadOnlyRequest{};
  return *instance;
}

AskingRequest::AskingRequest() {
  std::vector<RespValue> values(1);
  values[0].type(RespType::BulkString);
  values[0].asString() = "asking";
  type(RespType::Array);
  asArray().swap(values);
}

const AskingRequest& AskingRequest::instance() {
  static const AskingRequest* instance = new AskingRequest{};
  return *instance;
}

GetRequest::GetRequest() {
  type(RespType::BulkString);
  asString() = "get";
}

const GetRequest& GetRequest::instance() {
  static const GetRequest* instance = new GetRequest{};
  return *instance;
}

SetRequest::SetRequest() {
  type(RespType::BulkString);
  asString() = "set";
}

const SetRequest& SetRequest::instance() {
  static const SetRequest* instance = new SetRequest{};
  return *instance;
}
} // namespace Utility
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
