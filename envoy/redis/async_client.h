#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <map>

namespace Envoy {

namespace Event {

class Dispatcher;
}

namespace Redis {

struct AsyncClientConfig {
public:
  AsyncClientConfig(std::string&& username, std::string&& password, int op_timeout_milliseconds,
                    std::map<std::string, std::string>&& params)
      : auth_username_(std::move(username)), auth_password_(std::move(password)),
        op_timeout_(op_timeout_milliseconds), buffer_flush_timeout_(3), params_(std::move(params)) {
  }
  const std::string auth_username_;
  const std::string auth_password_;

  const std::chrono::milliseconds op_timeout_;
  const uint32_t max_buffer_size_before_flush_{1024};
  const std::chrono::milliseconds buffer_flush_timeout_;
  const uint32_t max_upstream_unknown_connections_{100};
  const bool enable_command_stats_{false};
  const std::map<std::string, std::string> params_;
};

/**
 * A handle to an outbound request.
 */
class PoolRequest {
public:
  virtual ~PoolRequest() = default;

  /**
   * Cancel the request. No further request callbacks will be called.
   */
  virtual void cancel() PURE;
};

class AsyncClient {
public:
  class Callbacks {
  public:
    virtual ~Callbacks() = default;

    virtual void onSuccess(std::string_view query, std::string&& response) PURE;

    virtual void onFailure(std::string_view query) PURE;
  };

  virtual ~AsyncClient() = default;

  virtual void initialize(AsyncClientConfig config) PURE;

  virtual PoolRequest* send(std::string&& query, Callbacks& callbacks) PURE;

  virtual Event::Dispatcher& dispatcher() PURE;
};

using AsyncClientPtr = std::unique_ptr<AsyncClient>;

} // namespace Redis
} // namespace Envoy
