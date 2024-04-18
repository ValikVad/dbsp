#pragma once

#include <cstdint>
#include <memory>

enum class OperationType { Read = 1, Write };
struct Request {
    size_t start_addr_;
    size_t size_bytes_;
    size_t time_;
    OperationType op_;
};

inline bool operator==(Request const& l, Request const& r) {
    return std::tie(l.start_addr_, l.size_bytes_) == std::tie(r.start_addr_, r.size_bytes_);
}

inline bool operator<(Request const& l, Request const& r) {
    return l.start_addr_ < r.start_addr_;
}

inline bool Valid(Request const& r) {
    return r.size_bytes_ != 0;
}

struct PrefetchedRequest : Request {
    double value;
};

inline bool operator<(PrefetchedRequest const& l, PrefetchedRequest const& r) {
    	if (l.value < r.value)
			return true;
		else if (l.value == r.value){
            if (l.start_addr_ < r.start_addr_)
			    return true;
		    else if (l.start_addr_ == r.start_addr_)
			    return (r.size_bytes_ == r.size_bytes_);
		    else
			    return false;
        }
		else	
			return false;
}

inline bool operator==(PrefetchedRequest const& l, PrefetchedRequest const& r) {
    return std::tie(l.start_addr_, l.size_bytes_) == std::tie(r.start_addr_, r.size_bytes_);
}

enum RequestSizeUpdatePolicy { ConstantByLimit, ConstantFirstValue, UpdateWithLatest, UpdateWithLargest, UpdateWithLargestWithLimit, UpdateWithSmallest };
enum TimeStamp {DoubleCounter, DoubleTime};
enum Metrics {OriginalPaper,  Module, NormalizedModul, MinModul, Square, NormalizedSquare, MinSquare };