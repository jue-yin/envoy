#include "source/extensions/filters/network/common/redis/raw_client_impl.h"

#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/filters/network/common/redis/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {
namespace {
Common::Redis::Client::DoNothingRawClientCallbacks null_raw_client_callbacks;
const std::string& RedisDBParamKey = "db";
} // namespace

RawClientPtr RawClientImpl::create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
                                   RawEncoderPtr&& encoder, RawDecoderFactory& decoder_factory,
                                   const Config& config,
                                   const RedisCommandStatsSharedPtr& redis_command_stats,
                                   Stats::Scope& scope) {
  auto client = std::make_unique<RawClientImpl>(
      host, dispatcher, std::move(encoder), decoder_factory, config, redis_command_stats, scope);
  client->connection_ = host->createConnection(dispatcher, nullptr, nullptr).connection_;
  client->connection_->addConnectionCallbacks(*client);
  client->connection_->addReadFilter(Network::ReadFilterSharedPtr{new UpstreamReadFilter(*client)});
  client->connection_->connect();
  client->connection_->noDelay(true);
  return client;
}

RawClientImpl::RawClientImpl(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
                             RawEncoderPtr&& encoder, RawDecoderFactory& decoder_factory,
                             const Config& config,
                             const RedisCommandStatsSharedPtr& redis_command_stats,
                             Stats::Scope& scope)
    : host_(host), encoder_(std::move(encoder)), decoder_(decoder_factory.create(*this)),
      config_(config),
      connect_or_op_timer_(dispatcher.createTimer([this]() { onConnectOrOpTimeout(); })),
      flush_timer_(dispatcher.createTimer([this]() { flushBufferAndResetTimer(); })),
      time_source_(dispatcher.timeSource()), redis_command_stats_(redis_command_stats),
      scope_(scope) {
  Upstream::ClusterTrafficStats& traffic_stats = *host->cluster().trafficStats();
  traffic_stats.upstream_cx_total_.inc();
  host->stats().cx_total_.inc();
  traffic_stats.upstream_cx_active_.inc();
  host->stats().cx_active_.inc();
  connect_or_op_timer_->enableTimer(host->cluster().connectTimeout());
}

RawClientImpl::~RawClientImpl() {
  ASSERT(pending_requests_.empty());
  ASSERT(connection_->state() == Network::Connection::State::Closed);
  Upstream::ClusterTrafficStats& traffic_stats = *host_->cluster().trafficStats();
  traffic_stats.upstream_cx_active_.dec();
  host_->stats().cx_active_.dec();
}

void RawClientImpl::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

void RawClientImpl::flushBufferAndResetTimer() {
  if (flush_timer_->enabled()) {
    flush_timer_->disableTimer();
  }
  connection_->write(encoder_buffer_, false);
}

PoolRequest* RawClientImpl::makeRawRequest(std::string_view request,
                                           RawClientCallbacks& callbacks) {
  ASSERT(connection_->state() == Network::Connection::State::Open);

  const bool empty_buffer = encoder_buffer_.length() == 0;

  Stats::StatName command = redis_command_stats_->getUnusedStatName();

  pending_requests_.emplace_back(*this, callbacks, command);
  encoder_->encode(request, encoder_buffer_);

  // If buffer is full, flush. If the buffer was empty before the request, start the timer.
  if (encoder_buffer_.length() >= config_.maxBufferSizeBeforeFlush()) {
    flushBufferAndResetTimer();
  } else if (empty_buffer) {
    flush_timer_->enableTimer(std::chrono::milliseconds(config_.bufferFlushTimeoutInMs()));
  }

  // Only boost the op timeout if:
  // - We are not already connected. Otherwise, we are governed by the connect timeout and the timer
  //   will be reset when/if connection occurs. This allows a relatively long connection spin up
  //   time for example if TLS is being used.
  // - This is the first request on the pipeline. Otherwise the timeout would effectively start on
  //   the last operation.
  if (connected_ && pending_requests_.size() == 1) {
    connect_or_op_timer_->enableTimer(config_.opTimeout());
  }

  return &pending_requests_.back();
}

void RawClientImpl::onConnectOrOpTimeout() {
  putOutlierEvent(Upstream::Outlier::Result::LocalOriginTimeout);

  Upstream::ClusterTrafficStats& traffic_stats = *host_->cluster().trafficStats();

  if (connected_) {
    traffic_stats.upstream_rq_timeout_.inc();
    host_->stats().rq_timeout_.inc();
  } else {
    traffic_stats.upstream_cx_connect_timeout_.inc();
    host_->stats().cx_connect_fail_.inc();
  }

  connection_->close(Network::ConnectionCloseType::NoFlush);
}

void RawClientImpl::onData(Buffer::Instance& data) {
  try {
    decoder_->decode(data);
  } catch (ProtocolError&) {
    Upstream::ClusterTrafficStats& traffic_stats = *host_->cluster().trafficStats();
    putOutlierEvent(Upstream::Outlier::Result::ExtOriginRequestFailed);
    traffic_stats.upstream_cx_protocol_error_.inc();
    host_->stats().rq_error_.inc();
    connection_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void RawClientImpl::putOutlierEvent(Upstream::Outlier::Result result) {
  if (!config_.disableOutlierEvents()) {
    host_->outlierDetector().putResult(result);
  }
}

void RawClientImpl::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {

    Upstream::reportUpstreamCxDestroy(host_, event);
    if (!pending_requests_.empty()) {
      Upstream::reportUpstreamCxDestroyActiveRequest(host_, event);
      if (event == Network::ConnectionEvent::RemoteClose) {
        putOutlierEvent(Upstream::Outlier::Result::LocalOriginConnectFailed);
      }
    }

    while (!pending_requests_.empty()) {
      PendingRequest& request = pending_requests_.front();
      if (!request.canceled_) {
        request.callbacks_.onFailure();
      } else {
        Upstream::ClusterTrafficStats& traffic_stats = *host_->cluster().trafficStats();
        traffic_stats.upstream_rq_cancelled_.inc();
      }
      pending_requests_.pop_front();
    }

    connect_or_op_timer_->disableTimer();
  } else if (event == Network::ConnectionEvent::Connected) {
    connected_ = true;
    ASSERT(!pending_requests_.empty());
    connect_or_op_timer_->enableTimer(config_.opTimeout());
  }

  if (event == Network::ConnectionEvent::RemoteClose && !connected_) {
    Upstream::ClusterTrafficStats& traffic_stats = *host_->cluster().trafficStats();
    traffic_stats.upstream_cx_connect_fail_.inc();
    host_->stats().cx_connect_fail_.inc();
  }
}

void RawClientImpl::onRawResponse(std::string&& response) {
  ASSERT(!pending_requests_.empty());
  PendingRequest& request = pending_requests_.front();
  const bool canceled = request.canceled_;

  request.aggregate_request_timer_->complete();

  RawClientCallbacks& callbacks = request.callbacks_;

  // We need to ensure the request is popped before calling the callback, since the callback might
  // result in closing the connection.
  pending_requests_.pop_front();
  if (canceled) {
    Upstream::ClusterTrafficStats& traffic_stats = *host_->cluster().trafficStats();
    traffic_stats.upstream_rq_cancelled_.inc();
  } else {
    // do not handle redirection here
    callbacks.onResponse(std::move(response));
  }

  // If there are no remaining ops in the pipeline we need to disable the timer.
  // Otherwise we boost the timer since we are receiving responses and there are more to flush
  // out.
  if (pending_requests_.empty()) {
    connect_or_op_timer_->disableTimer();
  } else {
    connect_or_op_timer_->enableTimer(config_.opTimeout());
  }

  putOutlierEvent(Upstream::Outlier::Result::ExtOriginRequestSuccess);
}

RawClientImpl::PendingRequest::PendingRequest(RawClientImpl& parent, RawClientCallbacks& callbacks,
                                              Stats::StatName command)
    : parent_(parent), callbacks_(callbacks), command_{command},
      aggregate_request_timer_(parent_.redis_command_stats_->createAggregateTimer(
          parent_.scope_, parent_.time_source_)) {
  Upstream::ClusterTrafficStats& traffic_stats = *parent.host_->cluster().trafficStats();
  traffic_stats.upstream_rq_total_.inc();
  parent.host_->stats().rq_total_.inc();
  traffic_stats.upstream_rq_active_.inc();
  parent.host_->stats().rq_active_.inc();
}

RawClientImpl::PendingRequest::~PendingRequest() {
  Upstream::ClusterTrafficStats& traffic_stats = *parent_.host_->cluster().trafficStats();
  traffic_stats.upstream_rq_active_.dec();
  parent_.host_->stats().rq_active_.dec();
}

void RawClientImpl::PendingRequest::cancel() {
  // If we get a cancellation, we just mark the pending request as cancelled, and then we drop
  // the response as it comes through. There is no reason to blow away the connection when the
  // remote is already responding as fast as possible.
  canceled_ = true;
}

void RawClientImpl::initialize(const std::string& auth_username, const std::string& auth_password,
                               const std::map<std::string, std::string>& params) {
  if (!auth_username.empty()) {
    std::string auth_request = Utility::makeRawAuthRequest(auth_username, auth_password);
    makeRawRequest(auth_request, null_raw_client_callbacks);
  } else if (!auth_password.empty()) {
    std::string auth_request = Utility::makeRawAuthRequest(auth_password);
    makeRawRequest(auth_request, null_raw_client_callbacks);
  }
  auto it = params.find(RedisDBParamKey);
  if (it != params.end()) {
    std::string select_request = Utility::makeSelectRequest(it->second);
    makeRawRequest(select_request, null_raw_client_callbacks);
  }

  if (config_.readPolicy() != Common::Redis::Client::ReadPolicy::Primary) {
    makeRawRequest(Utility::makeRawReadOnlyRequest(), null_raw_client_callbacks);
  }
}

RawClientFactoryImpl RawClientFactoryImpl::instance_;

RawClientPtr RawClientFactoryImpl::create(Upstream::HostConstSharedPtr host,
                                          Event::Dispatcher& dispatcher, const Config& config,
                                          const RedisCommandStatsSharedPtr& redis_command_stats,
                                          Stats::Scope& scope, const std::string& auth_username,
                                          const std::string& auth_password,
                                          const std::map<std::string, std::string>& params) {
  RawClientPtr client = RawClientImpl::create(host, dispatcher, RawEncoderPtr{new RawEncoderImpl()},
                                              decoder_factory_, config, redis_command_stats, scope);
  client->initialize(auth_username, auth_password, params);
  return client;
}

} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
