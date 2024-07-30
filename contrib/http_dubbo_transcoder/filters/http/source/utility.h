#pragma once
#include <iostream>
#include <vector>

#include "contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3/http_dubbo_transcoder.pb.h"
#include "contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3/http_dubbo_transcoder.pb.validate.h"
#include "envoy/http/codes.h"
#include "envoy/http/query_params.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/regex.h"
#include "source/common/http/utility.h"

#include "hessian2/basic_codec/object_codec.hpp"
#include "hessian2/object.hpp"
#include "include/nlohmann/json.hpp"
#include "transcoder.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HttpDubboTranscoder {

using json = nlohmann::json;
using Object = Hessian2::Object;
using ObjectPtr = std::unique_ptr<Object>;
using ListObject = Hessian2::UntypedListObject;
using ListObjectPtr = std::unique_ptr<ListObject>;
using StringObject = Hessian2::StringObject;
using StringObjectPtr = std::unique_ptr<StringObject>;
using MapObject = Hessian2::UntypedMapObject;
using MapObjectPtr = std::unique_ptr<MapObject>;

static const std::string ClassKey = "class";

static const absl::flat_hash_map<json::value_t, std::string> JsonType2JavaType{
    {json::value_t::number_integer, "java.lang.Long"},
    {json::value_t::number_unsigned, "java.lang.Long"},
    {json::value_t::string, "java.lang.String"},
    {json::value_t::boolean, "java.lang.Boolean"},
    {json::value_t::array, "java.util.List"},
    {json::value_t::object, "java.util.Map"},
    {json::value_t::number_float, "java.lang.Double"},
    {json::value_t::null, ""},
};

// the first byte in the response body express the response type
enum class RpcResponseType : uint8_t {
  ResponseWithException = 0,
  ResponseWithValue = 1,
  ResponseWithNullValue = 2,
  ResponseWithExceptionWithAttachments = 3,
  ResponseValueWithAttachments = 4,
  ResponseNullValueWithAttachments = 5,
};

class DubboUtility {
public:
  /***
   * this is a tool funtion convert a string view value to the given type value
   * now just support base type:
   * 1. Integer
   * 2. Boolean
   * 3. Double
   * 4. String
   *
   * other types will return absl::nullopt
   * @return the true value represent by json
   *
   */
  static absl::optional<json> convertStringToTypeValue(absl::string_view, std::string);
  static Http::Code convertStatusToHttpCode(absl::StatusCode status);

  // hessian2 json translate
  static json hessian2Json(Object* obj);
  static void json2Hessian(json&& j, Hessian2::Encoder& encoder);
  static void json2Hessian(json& j, Hessian2::Encoder& encoder);
  static void encodeParameterList(json& j, Hessian2::Encoder& encoder);
  static void createUntypedListObjcet(const json& object,
                                      Hessian2::Object::UntypedList& untyped_list);
  static std::string hessianType2String(Hessian2::Object::Type type);
  static json badCastErrorMessageJson(const std::string& type);
  static std::tuple<bool, bool, bool> resolveResponseFlag(RpcResponseType flag);
};

} // namespace HttpDubboTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
