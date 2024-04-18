#pragma once

#include <ipredictor.h>

#include <atomic>
#include <cassert>
#include <cmath>
#include <map>
#include <vector>

#include "config.h"
#ifdef PREFETCH_ENABLE_MULTI_THREADED
#    include <condition_variable>
#    include <mutex>
#    include <shared_mutex>
#    include <thread>
#endif

//TODO: assume time stamp type 'T' is integral for now
//      need to generailze to use std::chrono::time_point<>::min / max
//      in conjunction w/ 'std::numeric_limits'
template <typename T>
struct Entry : Request {
    using TimeStamp = T;
    std::vector<TimeStamp> mutable times;

    Entry() = default;
    Entry(Request const& r) : Request(r) {}

    void Update(TimeStamp ts) const {
        times.push_back(ts);
    }

    std::optional<std::tuple<size_t, size_t>> Association(Entry const& r, size_t lookahead, size_t confidence) const {
        if (std::abs(long(times.size()) - long(r.times.size())) > confidence)
            return std::nullopt;

        auto count = std::min(times.size(), r.times.size());
        assert(count);

        auto a = std::make_tuple(std::numeric_limits<T>::max(), std::numeric_limits<T>::min());
        for (size_t i = 1, error = 0; i < count; ++i) {
            auto delta = std::abs(times[i] - r.times[i]);
            if (delta > lookahead)
                ++error;

            if (error > confidence)
                return std::nullopt;

            a = std::make_tuple(std::min(std::get<0>(a), delta), std::max(std::get<1>(a), delta));
        }

        return a;
    }

    TimeStamp Stamp(size_t index) const {
        return times[index];
    }

    size_t Count() const {
        return times.size();
    }

    bool Valid() const {
        return !times.empty();
    }

    void Reset() {
        times.clear();
    }
};

class DBSP : public IPredictor, public IPredictorLink, public std::enable_shared_from_this<IPredictorLink> {
public:
    DBSP(const PredictorParams&);
    int init(const PredictorParams&);
    PredictorParams get_params(size_t);
    virtual ~DBSP();

private:
    std::shared_ptr<IPredictorLink> registerLink() override;
    std::shared_ptr<IPredictorLink> registerLink(void* /*owner*/, std::function<PredictorNotify>) override;

    int compute(Request, size_t) override;
    std::optional<Request> getAssociatedRequest(Request, double /*association_priority*/) override;
    std::vector<Request> getAssociatedVectorOfRequests(Request, double /*association_priority*/) ;

    bool CheckAvailable() const;

private:
    PredictorParams predicor_params;

    /** timestamp, currently reference number **/
    std::atomic<uint64_t> ts;

    struct RecordTable;
    struct PrefetchTable;
    using Record = Entry<int64_t>;

    std::unique_ptr<RecordTable> requests[2];
    RecordTable* r_requests;  //recording requests
    RecordTable* m_requests;  //mining requests

    std::unique_ptr<PrefetchTable> q_predictions;  //querying predictions
    std::unique_ptr<PrefetchTable> m_predictions;  //predictions under mining

    void record(Request);
    void do_mining();
    void notify();
    void mine();

#ifdef PREFETCH_ENABLE_MULTI_THREADED
    std::thread thread;
    std::mutex c_mutex;         //compute guard
    std::mutex n_mutex;         //callback guard
    std::shared_mutex m_mutex;  //mining guard
    std::condition_variable_any available;
#endif

    std::map<void*, std::function<PredictorNotify>> callbacks;
};
