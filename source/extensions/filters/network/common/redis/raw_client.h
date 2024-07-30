#pragma once

#include <cstdint>

#include "envoy/upstream/upstream.h"

#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/redis_command_stats.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {

class RawClientCallbacks {
public:
  virtual ~RawClientCallbacks() = default;

  virtual void onResponse(std::string&& value) PURE;

  virtual void onFailure() PURE;
};

class DoNothingRawClientCallbacks : public RawClientCallbacks {
public:
  // RawClientCallbacks
  void onFailure() override {}
  void onResponse(std::string&&) override {}
};

class RawClient : public Event::DeferredDeletable {
public:
  ~RawClient() override = default;

  /**
   * Adds network connection callbacks to the underlying network connection.
   */
  virtual void addConnectionCallbacks(Network::ConnectionCallbacks& callbacks) PURE;

  /**
   * Called to determine if the client has pending requests.
   * @return bool true if the client is processing requests or false if it is currently idle.
   */
  virtual bool active() PURE;

  /**
   * Closes the underlying network connection.
   */
  virtual void close() PURE;

  /**
   * Make a pipelined request to the remote redis server.
   * @param request supplies the RESP request to make.
   * @param callbacks supplies the request callbacks.
   * @return PoolRequest* a handle to the active request or nullptr if the request could not be made
   *         for some reason.
   */
  virtual PoolRequest* makeRawRequest(std::string_view request, RawClientCallbacks& callbacks) PURE;

  /**
   * Initialize the connection. Issue the auth command and readonly command as needed.
   * @param auth password for upstream host.
   */
  virtual void initialize(const std::string& auth_username, const std::string& auth_password,
                          const std::map<std::string, std::string>& params) PURE;
};

using RawClientPtr = std::unique_ptr<RawClient>;

class RawClientFactory {
public:
  virtual ~RawClientFactory() = default;

  virtual RawClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
                              const Config& config,
                              const RedisCommandStatsSharedPtr& redis_command_stats,
                              Stats::Scope& scope, const std::string& auth_username,
                              const std::string& auth_password,
                              const std::map<std::string, std::string>& params) PURE;
};

} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
