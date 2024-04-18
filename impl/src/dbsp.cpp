#include "dbsp.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

#include "utils.h"

inline size_t calc_size(size_t old_size, size_t new_size, PredictorParams const& params) {
    size_t max_v = std::max(old_size, new_size);
    switch (params.req_size_update_policy) {
    case RequestSizeUpdatePolicy::UpdateWithLatest:
        return new_size;
    case RequestSizeUpdatePolicy::UpdateWithLargest:
        return max_v;
    case RequestSizeUpdatePolicy::UpdateWithLargestWithLimit:
        return std::min(max_v, params.limit_size_for_size_policy);
    case RequestSizeUpdatePolicy::UpdateWithSmallest:
        return std::min(old_size, new_size);
    case RequestSizeUpdatePolicy::ConstantFirstValue:
        return old_size;
    case RequestSizeUpdatePolicy::ConstantByLimit:
        return params.limit_size_for_size_policy;
    default:
        assert(!"Unknown \'RequestSizeUpdatePolicy\'");
        return 0;
    }
}

namespace std {
template <typename T>
struct hash<Entry<T>*> {
    size_t operator()(Entry<T> const* r) const {
        return hash<size_t>{}(r->start_addr_);
    }
};

template <typename T>
struct equal_to<Entry<T>*> {
    size_t operator()(Entry<T> const* l, Entry<T> const* r) const {
        return r->start_addr_ == l->start_addr_;
    }
};
}  // namespace std

template <typename T>
inline bool Valid(Entry<T> const& r) {
    return r.Valid();
}

namespace std {
template <>
struct hash<Request*> {
    size_t operator()(Request const* r) const {
        return hash<size_t>{}(r->start_addr_);
    }
};

template <>
struct equal_to<Request*> {
    size_t operator()(Request const* l, Request const* r) const {
        return r->start_addr_ == l->start_addr_;
    }
};
}  // namespace std

class Prediction;

class DBSP::RecordTable : private LimitedHash<Record> {
public:
    using base = LimitedHash<Record>;
    RecordTable(size_t size = 2048, size_t size_m_table = 2048) : base(size), m_table(size_m_table) {}

    using table_type = LimitedQueue<Record>;
    table_type m_table;

    void Insert(Request request, typename Record::TimeStamp ts, PredictorParams const& params) {
        auto p = Push(request);
        auto r = p.first;
        DLOG(INFO) << FORMAT_REQUEST(r) << " is " << (p.second ? "inserted" : "found");

        r->size_bytes_ = calc_size(r->size_bytes_, request.size_bytes_, params);
        r->Update(ts);

        VLOG(3) << "Incoming " << FORMAT_REQUEST_WITH_TIME_STAMP(r);

        if (r->Count() == params.min_support) {
            VLOG(2) << "Move to M table " << FORMAT_REQUEST_WITH_TIME_STAMP(r);

            if (m_table.Full()) {
                auto f = m_table.Front();
                VLOG(2) << "Drop oldest request from M table " << FORMAT_REQUEST_WITH_TIME_STAMP(f);
                hash.erase(f);
                Extract(r, *f);
            } else {
                auto location = m_table.Push(Record{});
                Extract(r, *location);
            }
        } else if (r->Count() > params.max_support) {
            LOG_IF(ERROR, 0 == m_table.Size()) << "Empty MT!";
            VLOG(2) << "Drop too frequent " << FORMAT_REQUEST_WITH_TIME_STAMP(r);

            hash.erase(r);
            auto l = m_table.Back();
            hash.erase(l);
            if (r != l) {
                DLOG(INFO) << "Swapping " << FORMAT_REQUEST_WITH_TIME_STAMP(r) << " <=> " << FORMAT_REQUEST_WITH_TIME_STAMP(l);
                *r = std::move(*l);
                hash.insert(r);
            }
            m_table.Pop();
        }
    }

    template <typename F>
    auto Process(PredictorParams const& p, F f) {
        std::sort(std::begin(m_table), std::end(m_table), [](auto& l, auto& r) {
            return l.Stamp(0) < r.Stamp(0);
        });

        auto r = std::begin(m_table), l = std::end(m_table);
        for (; r != l; ++r) {
            hash.erase(&(*r));

            bool first = true;
            auto n = r;
            LimitedQueue<Request> associations(p.pf_list_size);
            for (++n; n != l; ++n) {
                if ((*n).Stamp(0) - (*r).Stamp(0) > p.lookahead_range)
                    break;

                auto a = (*r).Association(*n, p.lookahead_range, p.confidence);
                if (a) {
                    bool add = first || std::get<0>(*a) == 1;
                    if (first)
                        first = false;

                    if (add)
                        associations.Push(*n);
                }
            }

            f(*r, associations);
        };

        m_table.Clear();
    };

    //returns number of requests available for mining
    size_t Available() const {
        return m_table.Size();
    }

    using base::Size;
    using base::Find;
};

struct Prediction : Request {
    using container_type = LimitedHash<Request>;
    container_type associations;

    Prediction() : Request(), associations(0) {}
    Prediction(size_t size) : Request(), associations(size) {}
    Prediction(Request r, size_t size) : Request(r), associations(size) {}
};

namespace std {
template <>
struct hash<Prediction*> {
    size_t operator()(Prediction const* p) const {
        return hash<size_t>{}(p->start_addr_);
    }
};

template <>
struct equal_to<Prediction*> {
    size_t operator()(Prediction const* l, Prediction const* r) const {
        return r->start_addr_ == l->start_addr_;
    }
};
}  // namespace std

struct DBSP::PrefetchTable : private LimitedHash<Prediction> {
    using base = LimitedHash<Prediction>;
    size_t limit;

    PrefetchTable(size_t size, size_t limit) : base(size, Prediction{ limit }), limit(limit) {}

    Prediction* Find(Request r) {
        return base::Find(Prediction{ r, 0 });
    }

    Prediction* Push(Request r, Request a) {
        auto p = base::Push(Prediction{ r, limit }).first;
        assert(p);
        p->associations.Push(a);
        return p;
    }

    void Merge(PrefetchTable&& t) {
        std::for_each(std::begin(t.hash), std::end(t.hash), [&](auto x) {
            if (0 == x->associations.Size())
                return;

            auto p = base::Push(Prediction{ *x, limit }).first;
            assert(p);
            p->associations.Merge(std::move(x->associations));
        });

        t.Clear();
    }

    void Clear() {
        base::Clear();
        std::fill_n(table.data.get(), table.Capacity(), Prediction{ limit });
    }

    template <typename Iterator>
    Prediction* Append(Request r, Iterator begin, Iterator end) {
        auto p = base::Push(Prediction{ r, limit }).first;
        assert(p);
        std::for_each(begin, end, [p](auto a) {
            if (Valid(a))
                p->associations.Push(a);
        });

        return p;
    }

    template <typename F>
    void Notify(PredictorParams const& p, F f) {
        std::vector<Request> associations;
        associations.reserve(p.pf_list_size);

        std::for_each(std::begin(hash), std::end(hash), [&](auto p) {
            associations.clear();
            //notify only valid associations
            std::copy_if(std::begin(p->associations.table), std::end(p->associations.table), std::back_inserter(associations), [](auto r) {
                return Valid(r);
            });

            f(*p, std::data(associations), std::size(associations));
        });
    }

    using base::Size;
};

DBSP::DBSP(const PredictorParams&){};

PredictorParams DBSP::get_params(size_t bytes_total_size) {
    constexpr size_t default_pf_list_size = 2;
    constexpr size_t default_mining_table_num_rows = 1771;
#ifdef PREFETCH_ENABLE_MULTI_THREADED
    constexpr size_t default_thread_count = 1;
#else
    constexpr size_t default_thread_count = 0;
#endif
    // Let's define record/prefetch records length ratio as 5%. Need to keep in mind that we may have two record tables.
    constexpr auto record_prefech_tables_ratio = 5. / 100 * (default_thread_count ? 2 : 1);

    static_assert(std::is_same_v<RecordTable::base::table_type::value_type, Record>, "Type shall be Record");
    static_assert(std::is_same_v<RecordTable::base::hash_type::value_type, Record*>, "Type shall be Record*");
    static_assert(sizeof(std::__detail::_Hash_node<RecordTable::base::hash_type::value_type, true>) == 24, "Unexpected size");
    constexpr size_t record_table_entry_size = sizeof(RecordTable::base::table_type::value_type) +
                                               sizeof(std::__detail::_Hash_node<RecordTable::base::hash_type::value_type, true>) +
                                               // this one allocated by hash.bucket_count() value but we don't know exact num of buckets in advance :
                                               sizeof(std::__detail::_Hash_node_base*);
    constexpr size_t mining_table_entry_size = sizeof(RecordTable::table_type::value_type);

    static_assert(std::is_same_v<Prediction::container_type::table_type::value_type, Request>, "Type shall be Request");
    static_assert(std::is_same_v<Prediction::container_type::hash_type::value_type, Request*>, "Type shall be Request*");
    static_assert(sizeof(std::__detail::_Hash_node<Prediction::container_type::hash_type::value_type, true>) == 24, "Unexpected size");
    constexpr size_t prediction_entry_size = sizeof(Prediction) + sizeof(Prediction::container_type::table_type::value_type) * default_pf_list_size +
                                             sizeof(std::__detail::_Hash_node<Prediction::container_type::hash_type::value_type, true>) * default_pf_list_size +
                                             sizeof(std::__detail::_Hash_node_base*) * /* actually shall be 'hash.bucket_count()' */ default_pf_list_size;
    constexpr size_t prefetch_table_entry_size =
        prediction_entry_size + sizeof(std::__detail::_Hash_node<Prediction*, true>) + sizeof(std::__detail::_Hash_node_base*);

    PredictorParams par = {};
    par.req_size_update_policy = RequestSizeUpdatePolicy::UpdateWithLargest;
    par.limit_size_for_size_policy = 51200;
    par.thread_count = default_thread_count;
    par.min_support = 1;
    par.max_support = 13;
    par.lookahead_range = 160;
    par.pf_list_size = default_pf_list_size;
    par.mining_table_num_rows = default_mining_table_num_rows;
    par.confidence = 0;

    constexpr auto min_prefetch_entries = 1000U;
    constexpr size_t min_required_size = (prefetch_table_entry_size + mining_table_entry_size) * default_mining_table_num_rows +
                                         min_prefetch_entries * prefetch_table_entry_size +
                                         min_prefetch_entries * record_prefech_tables_ratio * record_table_entry_size;

    if (bytes_total_size < min_required_size)
        return {};

    size_t remaining_bytes = bytes_total_size - (mining_table_entry_size * par.mining_table_num_rows) -
                             (prefetch_table_entry_size * par.mining_table_num_rows) /* since we allocate two PrefetchTable's*/;

    // Equation of two vars 'record_table_num_rows' and 'prefetch_table_num_rows':
    //  bytes = record_table_num_rows * record_table_entry_size + prefetch_table_num_rows * prefetch_table_entry_size
    //  record_prefech_tables_ratio = record_table_num_rows / prefetch_table_num_rows

    // for simplicity let's express just for the prefetch table ...
    constexpr auto prefetch_table_coeff = 1 / (record_table_entry_size * record_prefech_tables_ratio + prefetch_table_entry_size);
    par.prefetch_table_num_rows = remaining_bytes * prefetch_table_coeff;
    remaining_bytes -= par.prefetch_table_num_rows * prefetch_table_entry_size;
    // .. and calc record table via remaining size
    par.record_table_num_rows = remaining_bytes / record_table_entry_size;

    if (default_thread_count) {
        // Since we have two tables, record_prefech_tables_ratio was increased 2x times.
        // So need to dictribute table length to two tables back:
        par.record_table_num_rows /= 2;
    }

    return par;
}

int DBSP::init(const PredictorParams& var) {
    predicor_params = var;

    ts = 0;
    size_t rsize = sizeof(Record) + sizeof(typename Record::TimeStamp) * predicor_params.max_support;
    size_t psize = sizeof(Prediction) + sizeof(Request) * predicor_params.pf_list_size;
    LOG(INFO) << "Constructing w/ params:"
              << "\n\tmining_table_num_rows {" << predicor_params.mining_table_num_rows << "}"
              << "\n\tmin/max {" << predicor_params.min_support << "," << predicor_params.max_support << "}"
              << "\n\tRT element/size/rows {" << sizeof(Record) << "," << rsize << "," << predicor_params.record_table_num_rows << "}"
              << "\n\tMT element/size/rows {" << sizeof(Record) << "," << rsize << "," << predicor_params.mining_table_num_rows << "}"
              << "\n\tRT element/size/rows {" << sizeof(Prediction) << "," << psize << "," << predicor_params.prefetch_table_num_rows << "}";

#if !defined(PREFETCH_ENABLE_MULTI_THREADED)
    if (predicor_params.thread_count > 0) {
        LOG(WARNING) << "Ignore requested threads' count N=" << predicor_params.thread_count << " at \'PREFETCH_ENABLE_MULTI_THREADED=OFF\' build.";
        predicor_params.thread_count = 0;
    }
#endif

    if (!predicor_params.thread_count) {
        LOG(WARNING) << "Forcing single threaded version (threads' count N=" << predicor_params.thread_count << ")";
        requests[0].reset(new RecordTable(predicor_params.record_table_num_rows, predicor_params.mining_table_num_rows));
        r_requests = m_requests = requests[0].get();
    } else {
#ifdef PREFETCH_ENABLE_MULTI_THREADED
        if (predicor_params.thread_count > 1) {
            LOG(WARNING) << "Ignore requested threads' count N=" << predicor_params.thread_count << ". Only 1 worker thread is supported for now.";
            predicor_params.thread_count = 1;
        }

        //TODO: maybe devide 'record_table_num_rows' by 2? otherwise we use x2 memory
        requests[0].reset(new RecordTable(predicor_params.record_table_num_rows, predicor_params.mining_table_num_rows));
        requests[1].reset(new RecordTable(predicor_params.record_table_num_rows, predicor_params.mining_table_num_rows));
        r_requests = requests[0].get();
        m_requests = requests[1].get();

        thread = std::thread(std::bind(&DBSP::mine, this));
#else
        std::cerr << "FATAL ERROR: threading is not available\n";
        std::terminate();
#endif
    }

    q_predictions.reset(new PrefetchTable(predicor_params.prefetch_table_num_rows, predicor_params.pf_list_size));
    m_predictions.reset(new PrefetchTable(predicor_params.mining_table_num_rows, predicor_params.pf_list_size));
    return 0;
}

DBSP::~DBSP() {
    LOG(INFO) << "Destructing";

#ifdef PREFETCH_ENABLE_MULTI_THREADED
    {
        std::unique_lock lock(m_mutex);
        predicor_params.mining_table_num_rows = 0;  //clear to notify mining thread to exit
        available.notify_one();
    }

    if (thread.joinable()) {
        thread.join();
    }
#endif
}

std::shared_ptr<IPredictorLink> DBSP::registerLink() {
    LOG(INFO) << "New link is registered";
    return shared_from_this();
}

std::shared_ptr<IPredictorLink> DBSP::registerLink(void* owner, std::function<PredictorNotify> n) {
    LOG(INFO) << "New link(owner=" << std::hex << owner << ") is registered";

#ifdef PREFETCH_ENABLE_MULTI_THREADED
    std::unique_lock lock(n_mutex);
#endif
    if (n)
        callbacks.emplace(owner, n);
    else
        callbacks.erase(owner);

    return shared_from_this();
}

int DBSP::compute(Request req, size_t) {
    ts++;
    record(req);

    return 0;
}

std::optional<Request> DBSP::getAssociatedRequest(Request req, double association_priority) {
    auto r = getAssociatedVectorOfRequests(req, association_priority);
    return r.empty() ? std::nullopt : std::make_optional(*r.begin());
}

std::vector<Request> DBSP::getAssociatedVectorOfRequests(Request request, double /*association_priority*/) {
    std::vector<Request> r;
    r.reserve(predicor_params.pf_list_size);

#ifdef PREFETCH_ENABLE_MULTI_THREADED
    std::shared_lock lock(m_mutex);
#endif

    auto p = q_predictions->Find(request);
    if (p)
        std::copy_if(std::begin(p->associations.table), std::end(p->associations.table), std::back_inserter(r), [](auto r) {
            return Valid(r);
        });

    if (VLOG_IS_ON(2) && !r.empty()) {
        VLOG(2) << "Querying associations " << FORMAT_REQUEST((&request));
        for (size_t i = 0; i < r.size(); ++i) {
            VLOG(2) << "#" << std::setw(predicor_params.pf_list_size) << i << ": " << FORMAT_REQUEST((&r[i])) << " ";
        };
    } else if (VLOG_IS_ON(3))
        VLOG(3) << "Querying associations " << FORMAT_REQUEST((&request));

    return r;
}

bool DBSP::CheckAvailable() const {
    return r_requests->Available() >= predicor_params.mining_table_num_rows;
}

void DBSP::mine() {
    while (true) {
#ifdef PREFETCH_ENABLE_MULTI_THREADED
        std::unique_lock lock(m_mutex);

        available.wait(lock, [&]() {
            return CheckAvailable();
        });
#endif

        if (!predicor_params.mining_table_num_rows)
            //exiting thread
            break;

        std::swap(r_requests, m_requests);

#ifdef PREFETCH_ENABLE_MULTI_THREADED
        lock.unlock();
#endif
        do_mining();
        notify();
#ifdef PREFETCH_ENABLE_MULTI_THREADED
        lock.lock();
#endif

        q_predictions->Merge(std::move(*m_predictions));
    }
}

void DBSP::record(Request req) {
#ifdef PREFETCH_ENABLE_MULTI_THREADED
    std::unique_lock c_lock(c_mutex);
    std::shared_lock m_lock(m_mutex);
#endif

    r_requests->Insert(req, ts.load(std::memory_order_relaxed), predicor_params);

    if (CheckAvailable()) {
#ifdef PREFETCH_ENABLE_MULTI_THREADED
        if (predicor_params.thread_count) {
            VLOG(3) << "Notify available (N=" << r_requests->Available() << ")";
            available.notify_one();
        } else
#else
        assert(0 == predicor_params.thread_count);
#endif
        {
            do_mining();
            notify();
            q_predictions->Merge(std::move(*m_predictions));
        }
    }
}

void DBSP::do_mining() {
    LOG_IF(ERROR, !m_requests->Available()) << "No request available for mining";

    VLOG(1) << "Mining RT{" << m_requests->Size() << "} MT{" << m_requests->Available() << "} PT{" << m_predictions->Size() << "}";
    m_requests->Process(predicor_params, [&](auto& r, auto& a) {
        m_predictions->Append(r, a.begin(), a.end());
    });
}

void DBSP::notify() {
#ifdef PREFETCH_ENABLE_MULTI_THREADED
    std::unique_lock lock(n_mutex);
#endif

    m_predictions->Notify(predicor_params, [&](Request r, Request const* associations, size_t size) {
        std::for_each(std::begin(callbacks), std::end(callbacks), [&](auto& c) {
            if (VLOG_IS_ON(3)) {
                VLOG(3) << "Notify " << std::hex << c.first << " for request " << FORMAT_REQUEST((&r));
                for (size_t i = 0; i < size; ++i) {
                    VLOG(3) << "#" << std::setw(size) << i << ": " << FORMAT_REQUEST((&associations[i])) << " ";
                };
            }

            c.second(r, associations, size);
        });
    });
}
