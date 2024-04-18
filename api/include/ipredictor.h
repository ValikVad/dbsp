#pragma once

#include <common.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

enum PredictorAlgo {
    PredictorAlgoAuto = 0u,  //implementation chooses the best suited
    PredictorAlgoMithrill = 1u,
    PredictorAlgoLookAhead = 2u
};

struct PredictorParams {
    // params for Mithril work
    size_t lookahead_range;
    size_t max_support;
    size_t min_support;
    size_t confidence;
    size_t pf_list_size;
    bool is_priority_queue;
    size_t dfs;

    TimeStamp ts_type;
    Metrics associations_metrics_type;

    // params for RequestSizeUpdatePolicy
    RequestSizeUpdatePolicy req_size_update_policy;
    size_t limit_size_for_size_policy;

    // params for size of Mithril tables
    size_t mining_table_num_rows;
    size_t prefetch_table_num_rows;
    size_t record_table_num_rows;

    //zero assumes single threaded
    size_t thread_count;

    unsigned algo;  //combination of PredictorMode flags
};

// This is the handle that consumers get after the registering.
// All the multithreading synchronization stuff shall be hidden inside the predictor implementation.
struct IPredictorLink {
    virtual ~IPredictorLink() = default;

    // provide data thru this
    // must not block caller thread for a long time
    virtual int compute(Request req, size_t timestamp = 0) = 0;

    // get associated request
    // small number of request priorities must be supported (e.g. 1 or 2)
    virtual std::optional<Request> getAssociatedRequest(Request req /* source request */, double association_priority = 0) = 0;

    virtual std::vector<Request> getAssociatedVectorOfRequests(Request req /* source request */, double association_priority = 0) = 0;
};

/* Type of predictor */
enum class PredictorType { DBSP};

/* Callback function that is invoked by predictor when associations are ready for request */
using PredictorNotify = int(Request /* request */, Request const* /* associations */, size_t /* associations count*/);

// Consumers shall be able just to `register` in the predictor and get a handle to provide incoming data and fetch prediction data
// A counsumer may be a shard, a thread, or a partition
// An instance per volume (or per pool?)
struct IPredictor {
    virtual ~IPredictor() = default;

    // predictor must support communicating with multiple
    // producer & consumer threads by means of links
    virtual std::shared_ptr<IPredictorLink> registerLink() = 0;
    // registers given owner's function to be invoked when associations are ready
    // passing NULL as function means de-register (removing) callback
    virtual std::shared_ptr<IPredictorLink> registerLink(void* /*owner*/, std::function<PredictorNotify>) = 0;
    static std::shared_ptr<IPredictor> create(PredictorType, const PredictorParams&);
        // Initializing object by parameters.
    virtual int init(const PredictorParams&) = 0;
};