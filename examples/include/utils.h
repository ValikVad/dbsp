#pragma once

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#include "csv.h"

struct customseps : std::numpunct<char> {
    char do_thousands_sep() const {
        return ' ';
    }

    std::string do_grouping() const {
        return "\3";
    }
};

enum TraceFileFormat { def = 0 };

std::istream& operator>>(std::istream& in, PrefetchPolicy& p) {
    std::string token;
    in >> token;

    boost::to_upper(token);

    if (token == "NEVER")
        p = PrefetchPolicy::Never;
    else if (token == "ALWAYS")
        p = PrefetchPolicy::Always;
    else if (token == "ONMISS")
        p = PrefetchPolicy::OnMiss;
    else
        in.setstate(std::ios_base::failbit);

    return in;
}

std::ostream& operator<<(std::ostream& out, CacheType& p) {
    if (p == CacheType::LRU) {
        out << "LRU";
    } else {
        out.setstate(std::ios_base::failbit);
    }
    return out;
}

std::istream& operator>>(std::istream& in, CacheType& p) {
    std::string token;
    in >> token;

    boost::to_upper(token);

    if (token == "LRU")
        p = CacheType::LRU;
    else
        in.setstate(std::ios_base::failbit);

    return in;
}

std::istream& operator>>(std::istream& in, PredictorType& p) {
    std::string token;
    in >> token;

    boost::to_upper(token);

    if (token == "DBSP")
        p = PredictorType::DBSP;

    return in;
}

std::istream& operator>>(std::istream& in, TraceFileFormat& f) {
    std::string token;
    in >> token;

    boost::to_upper(token);

    if (token == "def")
        f = TraceFileFormat::def;
    else
        in.setstate(std::ios_base::failbit);

    return in;
}

struct cache_line {

    int64_t ts; /* deprecated, should not use, virtual timestamp */
    int64_t size;
    int64_t real_time;
    // used to check whether current request is a valid request
    bool valid;

    int64_t op;
    int64_t unused_param1;
    int64_t unused_param2;

    void* extra_data;

    size_t block_unit_size;
    size_t disk_sector_size;
};

typedef struct cache_line request_t;


class TraceReader {
public:
    TraceReader(const char* file,  size_t num_requests, size_t num_skip, bool preload)
        : 
          _num_requests(num_requests),
          trace(file) {
        trace.read_header(io::ignore_missing_column, "ts", "hname", "d_number", "op", "adress", "size", "r_time");

        std::shared_ptr<request_t> req;
        size_t ts, d_number, address, size, r_time;
        std::string hname, op;

        if (num_skip){
            for (int i = 0; i < num_skip; i++){
                trace.read_row(ts, hname, d_number, op, address, size, r_time);
            }
        }

        auto r = Request{};
        std::cout << "\nTrace preloading started" << std::endl;
        
        while (trace.read_row(ts, hname, d_number, op, address, size, r_time)) {
            OperationType operation_type = OperationType::Write;
            if (op.compare(std::string("Read"))){
                OperationType operation_type = OperationType::Read;
            }
            r = Request{address, size, ts, operation_type};
            _requests.emplace_back(std::move(r));
        }
        _trace_length = _requests.size();
        std::cout << "Trace preloading finished" << std::endl;
    }

    ~TraceReader() = default;
    Request read_next() {
        Request r = {};
            if (_requests.size()) {
                r = _requests.front();
                _requests.pop_front();
            }

        ++_num_processed;

        return r;
    }
    size_t get_req_total_num() const {
        return _trace_length;
    }

private:
    TraceFileFormat _format = TraceFileFormat::def;
    const size_t _num_requests = 0;
    size_t _num_processed = 0;
    io::CSVReader<7> trace;
    size_t _trace_length = 0;

    std::deque<Request> _requests;
};