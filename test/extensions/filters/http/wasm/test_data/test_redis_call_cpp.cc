// NOLINT(namespace-envoy)
#include <memory>
#include <string>
#include <unordered_map>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics_lite.h"
#else
#include "source/extensions/common/wasm/ext/envoy_null_plugin.h"
#endif

START_WASM_PLUGIN(HttpWasmTestCpp)

class RedisCallContext : public Context {
public:
  explicit RedisCallContext(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
};

class RedisCallRootContext : public RootContext {
public:
  explicit RedisCallRootContext(uint32_t id, std::string_view root_id) : RootContext(id, root_id) {}

  bool onConfigure(size_t) override;
};

static RegisterContextFactory register_RedisCallContext(CONTEXT_FACTORY(RedisCallContext),
                                                        ROOT_FACTORY(RedisCallRootContext),
                                                        "redis_call");

bool RedisCallRootContext::onConfigure(size_t) {
  redisInit("cluster?db=1", "admin", "123456", 1000);
  return true;
}

FilterHeadersStatus RedisCallContext::onRequestHeaders(uint32_t, bool) {
  auto context_id = id();
  auto callback = [context_id](RedisStatus, size_t body_size) {
    if (body_size == 0) {
      logInfo("redis_call failed");
      return;
    }

    getContext(context_id)->setEffectiveContext();
    logWarn(std::string("bodysize: 5"));
    auto response = getBufferBytes(WasmBufferType::RedisCallResponse, 0, body_size);
    logDebug(std::string(response->view()));
  };

  // set id 1
  auto query = "*3\r\n$3\r\nset\r\n$2\r\nid\r\n$1\r\n1\r\n";
  auto path = getRequestHeader(":path");
  if (path->view() == "/bad") {
    if (root()->redisCall("cluster?db=1", query, callback) != WasmResult::Ok) {
      logInfo("redis_call rejected");
    }
  } else {
    if (root()->redisCall("bogus cluster", query, callback) == WasmResult::Ok) {
      logError("bogus cluster found error");
    }
    root()->redisCall("cluster?db=1", query, callback);
    logInfo("onRequestHeaders");
  }

  return FilterHeadersStatus::StopIteration;
}

END_WASM_PLUGIN
