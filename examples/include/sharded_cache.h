#pragma once

#include <icache.h>

#include <vector>

#include "worker.h"

class ShardedCache {
public:
    ShardedCache(CacheType t, PrefetchPolicy p, PredictorType pt, size_t num_shards, size_t shard_size)
        : _type(t),
          _prefetch_policy(p),
          _predictor_type(pt),
          _num_shards(num_shards),
          _shard_size(shard_size) {}

    void Init(const CacheParams&, const PredictorParams&, bool);
    PredictorParams get_predictor_params(size_t);
    virtual ~ShardedCache() = default;

    std::vector<std::future<Response>> Process(const Request& r) {

            auto on_prediction = [&](const Request& r) -> void {
                auto res = Prefetch(r);

                std::lock_guard<std::mutex> lock(_mutex);
                std::move(std::begin(res), std::end(res), std::back_inserter(_cached_response));
            };

            auto read = [this, on_prediction](const Request& r, uint8_t idx) -> Response {
                return _caches[idx]->Read(r, on_prediction);
            };
            return DispatchToShard(r, read, false);
    }

private:
    std::vector<std::future<Response>> DispatchToShard(const Request& r, std::function<Response(const Request&, uint8_t)> f, bool pushToFront);
    std::vector<std::future<Response>> Prefetch(const Request& r) {
        auto prefetch = [this](const Request& r, uint8_t idx) -> Response {
            return _caches[idx]->Prefetch(r);
        };
        return DispatchToShard(r, prefetch, true);
    }

    CacheType _type;
    PrefetchPolicy _prefetch_policy;
    PredictorType _predictor_type;
    size_t _num_shards;
    size_t _shard_size;
    CacheParams _cache_par;
    uint32_t _blocks_in_shard;
    std::vector<std::unique_ptr<ICache>> _caches;
    std::vector<std::unique_ptr<Worker>> _threads;

    std::mutex _mutex;
    std::vector<std::future<Response>> _cached_response;
};
