#pragma once

#include <common.h>
#include <ipredictor.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace cache {

struct Hits {
    uint32_t val;
};

struct Misses {
    uint32_t val;
};

struct Prefetched {
    uint32_t val;
};

struct EvictedUnused {
    uint32_t val;
};

struct Latency {
    uint32_t val;
};

struct InternalNumRequest {
    uint32_t val;
};

}  // namespace cache

using Response = std::tuple<cache::Hits, cache::Misses, cache::Prefetched, cache::EvictedUnused, cache::Latency, cache::InternalNumRequest>;

struct CacheParams {
    size_t cache_size;
    size_t page_size;
    size_t block_size;
};

class ICache {
public:
    virtual void Init(const CacheParams&, std::shared_ptr<IPredictorLink>) = 0;  // size in bytes

    virtual ~ICache() = default;

    virtual Response Write(const Request&) = 0;
    virtual Response Read(const Request&, std::function<void(const Request&)> on_prediction = nullptr) = 0;
    virtual Response Prefetch(const Request&) = 0;
};

/* Type of cache (eviction) */
enum class CacheType { LRU, AMP };

enum class PrefetchPolicy { Never, Always, OnMiss };

std::unique_ptr<ICache> CreateCache(CacheType, PrefetchPolicy = PrefetchPolicy::Never);
