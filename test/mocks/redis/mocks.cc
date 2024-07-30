#include "mocks.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Redis {

MockRedisAsyncClient::MockRedisAsyncClient() {
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
}
MockRedisAsyncClient::~MockRedisAsyncClient() = default;

MockRedisPoolRequest::MockRedisPoolRequest(MockRedisAsyncClient* client, std::string&& request)
    : client_(client), request_(request) {}
MockRedisPoolRequest::~MockRedisPoolRequest() = default;

MockRedisAsyncClientCallbacks::MockRedisAsyncClientCallbacks() = default;
MockRedisAsyncClientCallbacks::~MockRedisAsyncClientCallbacks() = default;

} // namespace Redis
} // namespace Envoy
