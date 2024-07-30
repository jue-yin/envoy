#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/redis/async_client.h"
#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"

#include "source/common/http/utility.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/printers.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gmock/gmock.h"

using testing::Return;

namespace Envoy {
namespace Redis {

class MockRedisAsyncClient : public Redis::AsyncClient {
public:
  MockRedisAsyncClient();
  ~MockRedisAsyncClient() override;

  Redis::PoolRequest* send(std::string&& query, Callbacks& callbacks) override {
    return send_(query, callbacks);
  }

  MOCK_METHOD(void, initialize, (Redis::AsyncClientConfig config), (override));

  MOCK_METHOD(Redis::PoolRequest*, send_, (std::string & query, Callbacks& callbacks));

  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));

  NiceMock<Event::MockDispatcher> dispatcher_;
};

class MockRedisPoolRequest : public Redis::PoolRequest {
public:
  MockRedisPoolRequest(MockRedisAsyncClient* client, std::string&& request);
  ~MockRedisPoolRequest() override;

  MOCK_METHOD(void, cancel, ());

  MockRedisAsyncClient* client_;
  std::string request_;
};

class MockRedisAsyncClientCallbacks : public Redis::AsyncClient::Callbacks {
public:
  MockRedisAsyncClientCallbacks();
  ~MockRedisAsyncClientCallbacks() override;

  // Redis::AsyncClient::Callbacks
  void onSuccess(std::string_view query, std::string&& response) override {
    onSuccess_(query, response);
  }
  MOCK_METHOD(void, onFailure, (std::string_view query), (override));

  MOCK_METHOD(void, onSuccess_, (std::string_view query, std::string& response));
};

} // namespace Redis
} // namespace Envoy
