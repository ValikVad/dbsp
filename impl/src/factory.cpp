#include <typeinfo>


#include "cache.h"
#include "lru.h"
#include "dbsp.h"

std::unique_ptr<ICache> CreateCache(CacheType t, PrefetchPolicy p) {
    if (t == CacheType::LRU) {
        return std::make_unique<Cache<LruCache>>(t, p);
    } 
    return std::unique_ptr<ICache>();
}

// std::shared_ptr<IPredictor> IPredictor::create(const PredictorType& t) {
//     switch (t) {
//     case PredictorType::DBSP:
//         return std::make_shared<DBSP>();
//     default:
//         assert(!"Unknown predictor type");
//         return std::shared_ptr<IPredictor>(nullptr);
//     }
// }

std::shared_ptr<IPredictor> IPredictor::create(PredictorType t, const PredictorParams& p) {
    PredictorParams par(p);
    switch (t) {
    case PredictorType::DBSP:
        return std::make_shared<DBSP>(par);
    default:
        assert(!"Unknown predictor type");
        return std::shared_ptr<IPredictor>(nullptr);
    }
}


