#include "lru.h"

#include <algorithm>
#include <cmath>
#include <iostream>

void LruCache::Init(const CacheParams& par) {
    VerifyParams(par);

    _par = par;

    _cache = std::make_unique<lru_cache<size_t, page_w_blocks_t>>(_par.cache_size / _par.page_size);
}

void LruCache::Verify(const Request& r) const {
    VerifyRequest(r, _par);
}

Response LruCache::Write(const Request& r) {
    Verify(r);

    uint32_t num_evicted_untouched = 0;

    std::for_each(PageIterator(_par, r), PageIterator(), [this, &num_evicted_untouched](auto& page_it) {
        auto opt = _cache->get(page_it.id);
        auto [existed_page, evicted_page] = opt ? std::make_pair(opt.value(), std::nullopt) : _cache->put(page_it.id, {});

        if (evicted_page) {
            for (auto& blk : evicted_page.value().second) {
                num_evicted_untouched += (std::get<IsFromPredictor>(blk.second).val == true) && (std::get<NumReads>(blk.second).val == 0);
            }
        }

        for (auto x = 0; x < page_it.num_blocks; ++x) {
            if (auto block_it = existed_page.find(page_it.block_id + x); block_it == existed_page.end()) {
                existed_page.insert({ page_it.block_id + x, std::make_tuple(IsFromPredictor{ false }, NumReads{ 0 }) });
            }
        }
    });

    return std::make_tuple(cache::Hits{ 0 },
                           cache::Misses{ 0 },
                           cache::Prefetched{ 0 },
                           cache::EvictedUnused{ num_evicted_untouched },
                           cache::Latency{ 0 },
                           cache::InternalNumRequest{ 0 });
}

Response LruCache::Read(const Request& r) {
    Verify(r);

    uint32_t num_cache_hits = 0;
    uint32_t num_cache_misses = 0;
    uint32_t num_evicted_untouched = 0;

    std::for_each(PageIterator(_par, r), PageIterator(), [this, &num_cache_hits, &num_cache_misses, &num_evicted_untouched](auto& page_it) {
        auto opt = _cache->get(page_it.id);
        auto [existed_page, evicted_page] = opt ? std::make_pair(opt.value(), std::nullopt) : _cache->put(page_it.id, {});

        if (evicted_page) {
            for (auto& blk : evicted_page.value().second) {
                num_evicted_untouched += (std::get<IsFromPredictor>(blk.second).val == true) && (std::get<NumReads>(blk.second).val == 0);
            }
        }

        for (auto x = 0; x < page_it.num_blocks; ++x) {
            if (auto block_it = existed_page.find(page_it.block_id + x); block_it != existed_page.end()) {
                ++num_cache_hits;
                auto& blk = block_it->second;
                ++std::get<NumReads>(blk).val;
            } else {
                ++num_cache_misses;
                existed_page.insert({ page_it.block_id + x, std::make_tuple(IsFromPredictor{ false }, NumReads{ 0 }) });
            }
        }
    });

    return std::make_tuple(cache::Hits{ num_cache_hits },
                           cache::Misses{ num_cache_misses },
                           cache::Prefetched{ 0 },
                           cache::EvictedUnused{ num_evicted_untouched },
                           cache::Latency{ 0 },
                           cache::InternalNumRequest{ 0 });
}

Response LruCache::Prefetch(const Request& r) {
    Verify(r);

    uint32_t num_prefetched = 0;
    uint32_t num_evicted_untouched = 0;

    std::for_each(PageIterator(_par, r), PageIterator(_par), [this, &num_prefetched, &num_evicted_untouched](auto& page_it) {
        auto opt = _cache->get(page_it.id);
        auto [existed_page, evicted_page] = opt ? std::make_pair(opt.value(), std::nullopt) : _cache->put(page_it.id, {});

        if (evicted_page) {
            for (auto& blk : evicted_page.value().second) {
                num_evicted_untouched += (std::get<IsFromPredictor>(blk.second).val == true) && (std::get<NumReads>(blk.second).val == 0);
            }
        }

        for (auto x = 0; x < page_it.num_blocks; ++x) {
            if (auto block_it = existed_page.find(page_it.block_id + x) == existed_page.end()) {
                ++num_prefetched;
                existed_page.insert({ page_it.block_id + x, std::make_tuple(IsFromPredictor{ true }, NumReads{ 0 }) });
            }
        }
    });

    return std::make_tuple(cache::Hits{ 0 },
                           cache::Misses{ 0 },
                           cache::Prefetched{ num_prefetched },
                           cache::EvictedUnused{ num_evicted_untouched },
                           cache::Latency{ 0 },
                           cache::InternalNumRequest{ 0 });
}
