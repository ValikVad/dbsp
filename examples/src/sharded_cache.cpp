#include "sharded_cache.h"

PredictorParams ShardedCache::get_predictor_params(size_t predictor_size_in_bytes) {
    PredictorParams pp = PredictorParams();
    // std::shared_ptr<IPredictor> predictor = IPredictor::create(_predictor_type, pp);
    // pp = predictor->get_params(predictor_size_in_bytes);
    return pp;
}

void ShardedCache::Init(const CacheParams& par, const PredictorParams& pp, bool bShardedPredictor) {
    _cache_par = par;
    _blocks_in_shard = _shard_size / _cache_par.block_size;

    CacheParams new_par = _cache_par;
    new_par.cache_size = _num_shards ? _cache_par.cache_size / _num_shards : _cache_par.cache_size;

    std::vector<std::shared_ptr<IPredictorLink>> predictors(_num_shards ? _num_shards : 1);
    if (bShardedPredictor) {
        for (auto& predictorlink : predictors) {
            std::shared_ptr<IPredictor> predictor = IPredictor::create(_predictor_type, pp);
            predictor->init(pp);
            predictorlink = std::move(_prefetch_policy != PrefetchPolicy::Never ? predictor->registerLink() : nullptr);
        }
    } else {
        std::shared_ptr<IPredictor> predictor = IPredictor::create(_predictor_type, pp);
        predictor->init(pp);
        auto p = std::move(_prefetch_policy != PrefetchPolicy::Never ? predictor->registerLink() : nullptr);
        std::fill(predictors.begin(), predictors.end(), p);
    }

    if (_num_shards) {
        for (size_t i = 0; i < _num_shards; ++i) {
            auto c = CreateCache(_type, _prefetch_policy);
            c->Init(new_par, std::move(predictors[i]));
            _caches.emplace_back(std::move(c));

            auto t = std::make_unique<Worker>();
            t->Start();
            _threads.emplace_back(std::move(t));
        }
    } else {
        auto c = CreateCache(_type, _prefetch_policy);
        c->Init(new_par, std::move(predictors[0]));
        _caches.emplace_back(std::move(c));
    }
}

std::vector<std::future<Response>> ShardedCache::DispatchToShard(const Request& r, std::function<Response(const Request&, uint8_t)> action, bool pushtoFront) {
    std::vector<std::future<Response>> res;

    if (0 == _num_shards) {
        std::packaged_task<Response()> task([&] {
            return action(r, 0);
        });
        res.emplace_back(task.get_future());
        task();
    } else {
        auto shard_idx = r.start_addr_ / _shard_size;
        auto range = std::make_pair<size_t, size_t>(r.start_addr_ / _cache_par.block_size,
                                                    r.start_addr_ / _cache_par.block_size + r.size_bytes_ / _cache_par.block_size);

        while (range.first < range.second) {
            const auto current_shard = std::make_pair<size_t, size_t>(shard_idx * _blocks_in_shard, (shard_idx + 1) * _blocks_in_shard);
            const auto start_idx_inside_shard = range.first - current_shard.first;
            const auto end_idx_inside_shard =
                (current_shard.first < range.second) && (range.second < current_shard.second) ? (range.second - current_shard.first) : _blocks_in_shard;
            const auto block_num_inside_shard = end_idx_inside_shard - start_idx_inside_shard;

            const auto idx = shard_idx % _num_shards;
            auto f = [action, idx](const Request& r) -> Response {
                return action(r, idx);
            };

            const auto shard_request = Request{ range.first * _cache_par.block_size, block_num_inside_shard * _cache_par.block_size};
            res.emplace_back(_threads[idx]->AddTask(pushtoFront, f, shard_request));

            range.first += block_num_inside_shard;
            ++shard_idx;
        }
    }

    std::lock_guard<std::mutex> lock(_mutex);
    std::move(std::begin(_cached_response), std::end(_cached_response), std::back_inserter(res));
    _cached_response.clear();

    return res;
}
