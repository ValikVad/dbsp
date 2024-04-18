#pragma once

#include <icache.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>

template <typename T>
class Cache : public ICache {
public:
    Cache(CacheType t, PrefetchPolicy p = PrefetchPolicy::Always) {
        _prefetch_policy = p;
    }

    virtual void Init(const CacheParams& par, std::shared_ptr<IPredictorLink> l) {
        _predictor = l;

        if (_predictor.get() == nullptr)
            _prefetch_policy = PrefetchPolicy::Never;

        return _impl.Init(par);
    }

    virtual ~Cache() = default;

    virtual Response Write(const Request& r) {
        return _impl.Write(r);
    }

    virtual Response Read(const Request& r, std::function<void(const Request&)> action_on_prediction) {
        auto hit_count = _impl.Read(r);

        auto start = std::chrono::system_clock::now();
        if (PrefetchPolicy::Never != _prefetch_policy) {
            if (_predictor->compute(r, (size_t)0))
                throw std::runtime_error("predictor->compute failed\n");
        }

        if (PrefetchPolicy::Always == _prefetch_policy || (PrefetchPolicy::OnMiss == _prefetch_policy && std::get<cache::Misses>(hit_count).val != 0)) {
            auto prediction = _predictor->getAssociatedVectorOfRequests(r);
            std::for_each(std::begin(prediction), std::end(prediction), [&action_on_prediction](auto r) {
                action_on_prediction(r);
            });
        }

        auto diff = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - start).count();
        std::get<cache::Latency>(hit_count).val = diff;

        return hit_count;
    }

    virtual Response Prefetch(const Request& r) {
        return _impl.Prefetch(r);
    }

private:
    T _impl;
    std::shared_ptr<IPredictorLink> _predictor;
    PrefetchPolicy _prefetch_policy;
};