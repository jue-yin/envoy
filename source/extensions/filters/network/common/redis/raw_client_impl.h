#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/filter_impl.h"
#include "source/extensions/filters/network/common/redis/raw_client.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {

class RawClientImpl : public RawClient,
                      public RawDecoderCallbacks,
                      public Network::ConnectionCallbacks {
public:
  static RawClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
                             RawEncoderPtr&& encoder, RawDecoderFactory& decoder_factory,
                             const Config& config,
                             const RedisCommandStatsSharedPtr& redis_command_stats,
                             Stats::Scope& scope);

  RawClientImpl(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
                RawEncoderPtr&& encoder, RawDecoderFactory& decoder_factory, const Config& config,
                const RedisCommandStatsSharedPtr& redis_command_stats, Stats::Scope& scope);
  ~RawClientImpl() override;

  // RawClient
  void addConnectionCallbacks(Network::ConnectionCallbacks& callbacks) override {
    connection_->addConnectionCallbacks(callbacks);
  }
  void close() override;
  PoolRequest* makeRawRequest(std::string_view request, RawClientCallbacks& callbacks) override;
  bool active() override { return !pending_requests_.empty(); }
  void flushBufferAndResetTimer();
  void initialize(const std::string& auth_username, const std::string& auth_password,
                  const std::map<std::string, std::string>& params) override;

private:
  friend class RedisRawClientImplTest;

  struct UpstreamReadFilter : public Network::ReadFilterBaseImpl {
    UpstreamReadFilter(RawClientImpl& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      parent_.onData(data);
      return Network::FilterStatus::Continue;
    }

    RawClientImpl& parent_;
  };

  struct PendingRequest : public PoolRequest {
    PendingRequest(RawClientImpl& parent, RawClientCallbacks& callbacks, Stats::StatName stat_name);
    ~PendingRequest() override;

    // PoolRequest
    void cancel() override;

    RawClientImpl& parent_;
    RawClientCallbacks& callbacks_;
    Stats::StatName command_;
    bool canceled_{};
    Stats::TimespanPtr aggregate_request_timer_;
    Stats::TimespanPtr command_request_timer_;
  };

  void onConnectOrOpTimeout();
  void onData(Buffer::Instance& data);
  void putOutlierEvent(Upstream::Outlier::Result result);

  // RawDecoderCallbacks
  void onRawResponse(std::string&& response) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  Upstream::HostConstSharedPtr host_;
  Network::ClientConnectionPtr connection_;
  RawEncoderPtr encoder_;
  Buffer::OwnedImpl encoder_buffer_;
  DecoderPtr decoder_;
  const Config& config_;
  std::list<PendingRequest> pending_requests_;
  Event::TimerPtr connect_or_op_timer_;
  bool connected_{};
  Event::TimerPtr flush_timer_;
  Envoy::TimeSource& time_source_;
  const RedisCommandStatsSharedPtr redis_command_stats_;
  Stats::Scope& scope_;
};

class RawClientFactoryImpl : public RawClientFactory {
public:
  RawClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
                      const Config& config, const RedisCommandStatsSharedPtr& redis_command_stats,
                      Stats::Scope& scope, const std::string& auth_username,
                      const std::string& auth_password,
                      const std::map<std::string, std::string>& params) override;

  static RawClientFactoryImpl instance_;

private:
  RawDecoderFactoryImpl decoder_factory_;
};

} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
