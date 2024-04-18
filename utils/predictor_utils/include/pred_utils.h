#include <common.h>
#include <ipredictor.h>

enum params_cases {
    UnitTestCase,
    OriginalPaperCase,
    Master,
    Counter_Module,
    Counter_NormalizedModul,
    Counter_MinModul,
    Counter_Square,
    Counter_NormalizedSquare,
    Counter_MinSquare,
    Time_NormalizedModul,
};

namespace PredictorUtils {
inline PredictorParams get_predictor_params(params_cases c) {
    PredictorParams param{};
    switch (c) {
    case params_cases::UnitTestCase:
        param.lookahead_range = 3;
        param.max_support = 5;
        param.min_support = 2;
        param.confidence = 0;
        param.pf_list_size = 2;
        param.mining_table_num_rows = 3;
        param.prefetch_table_num_rows = 1000;
        param.record_table_num_rows = 2000;
        param.req_size_update_policy = RequestSizeUpdatePolicy::UpdateWithLargest;
        param.limit_size_for_size_policy = 512;
        param.thread_count = 0;
        param.algo = PredictorAlgo::PredictorAlgoAuto;
        param.ts_type = TimeStamp::DoubleCounter;
        param.associations_metrics_type = Metrics::OriginalPaper;
        param.is_priority_queue =false;
        param.dfs = 0;
        break;

    case params_cases::OriginalPaperCase:
        param.lookahead_range = 20;
        param.max_support = 8;
        param.min_support = 2;
        param.confidence = 0;
        param.pf_list_size = 2;
        param.mining_table_num_rows = 2560;
        param.prefetch_table_num_rows = 30e3;
        param.record_table_num_rows = 20e3;
        param.req_size_update_policy = RequestSizeUpdatePolicy::UpdateWithLargest;
        param.limit_size_for_size_policy = 0;
        param.thread_count = 0;
        param.algo = PredictorAlgo::PredictorAlgoMithrill;
        param.ts_type = TimeStamp::DoubleCounter;
        param.associations_metrics_type = Metrics::OriginalPaper;
        param.is_priority_queue =false;
        param.dfs = 1;
        break;

    case params_cases::Master:
        param.lookahead_range = 220;
        param.max_support = 20;
        param.min_support = 1;
        param.confidence = 0;
        param.pf_list_size = 2;
        param.mining_table_num_rows = 1750;
        param.prefetch_table_num_rows = 20000000;
        param.record_table_num_rows = 200000;
        param.req_size_update_policy = RequestSizeUpdatePolicy::UpdateWithLargest ;
        param.limit_size_for_size_policy = 512 * 100;
        param.thread_count = 0;
        param.algo = PredictorAlgo::PredictorAlgoAuto;
        param.ts_type = TimeStamp::DoubleCounter;
        param.associations_metrics_type = Metrics::OriginalPaper;
        param.is_priority_queue = false;
        param.dfs = 1;
        break;

    case params_cases::Counter_NormalizedModul:
        param.lookahead_range = 20;
        param.max_support = 20;
        param.min_support = 1;
        param.confidence = 0;
        param.pf_list_size = 2;
        param.mining_table_num_rows = 1750;
        param.prefetch_table_num_rows = 20000000;
        param.record_table_num_rows = 200000;
        param.req_size_update_policy = RequestSizeUpdatePolicy::UpdateWithLargest ;
        param.limit_size_for_size_policy = 512 * 100;
        param.thread_count = 0;
        param.algo = PredictorAlgo::PredictorAlgoAuto;
        param.ts_type = TimeStamp::DoubleCounter;
        param.associations_metrics_type = Metrics::NormalizedModul;
        param.is_priority_queue = true;
        param.dfs = 1;
        break;
    
    case params_cases::Time_NormalizedModul:
        param.lookahead_range = 20;
        param.max_support = 20;
        param.min_support = 1;
        param.confidence = 0;
        param.pf_list_size = 2;
        param.mining_table_num_rows = 1750;
        param.prefetch_table_num_rows = 20000000;
        param.record_table_num_rows = 200000;
        param.req_size_update_policy = RequestSizeUpdatePolicy::UpdateWithLargest ;
        param.limit_size_for_size_policy = 512 * 100;
        param.thread_count = 0;
        param.algo = PredictorAlgo::PredictorAlgoAuto;
        param.ts_type = TimeStamp::DoubleTime;
        param.associations_metrics_type = Metrics::NormalizedModul;
        param.is_priority_queue = true;
        param.dfs = 1;
        break;
    default:
        throw std::runtime_error("Unknown \'params_cases\' value");
    }

    return param;
}
};  // namespace PredictorUtils