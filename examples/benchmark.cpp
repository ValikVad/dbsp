#include <icache.h>
#include <ipredictor.h>

#include <filesystem>
#include <iostream>

#include "pred_utils.h"
#include "csv.h"
#include "sharded_cache.h"
#include "utils.h"
#include "worker.h"

namespace po = boost::program_options;
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    std::cout << "Git commit: " << GIT_COMMIT_HASH << std::endl;
    std::cout << "Build date: " << BUILD_DATE << std::endl;

    constexpr size_t default_size = 200 * 1024 * 1024;
    constexpr size_t default_page = 64 * 1024;
    constexpr size_t default_block = 512;
    constexpr size_t default_shard = 2 * 1024 * 1024;

    fs::path input;
    CacheParams cache_par;
    size_t num_shards, num_requests, skip, max_queue_size, shard_size;
    int verbose;
    auto prefetch_policy = PrefetchPolicy::Never;
    auto predictor_type = PredictorType::DBSP;
    size_t pr_metadata_size_bytes = 0;
    PredictorParams par = PredictorUtils::get_predictor_params(params_cases::OriginalPaperCase);
    auto latency = false;
    auto pr_auto_config = false;
    auto preload_trace = false;
    auto sharded_predictor = false;
    auto trace_format = TraceFileFormat::def;
    auto cache_type = CacheType::LRU;
    po::options_description desc("Allowed options");

    // clang-format off
    desc.add_options()
    ("help", "produce help message")("input,I", po::value<>(&input), "Path to trace data")
    ("format", po::value<TraceFileFormat>(&trace_format), "Format of an input trace file, default is def")
    ("cache,C", po::value<>(&cache_par.cache_size)->default_value(default_size), "Cache size")("shards,N", po::value<>(&num_shards)->default_value(0), "Number of shards (zero means sharding is off)")
    ("cache_type", po::value<CacheType>(&cache_type), "Type of cache, default is LRU\nPossible values:\n0) LRU")
    ("shard_size",  po::value<>(&shard_size)->default_value(default_shard), "Shard size (in bytes)")
    ("requests,R", po::value<>(&num_requests)->default_value(-1), "Number of requests to proceed")
    ("skip,S", po::value<>(&skip)->default_value(0), "Number of requests to skip")
    ("page,P", po::value<>(&cache_par.page_size)->default_value(default_page), "Page size")
    ("block,B",  po::value<>(&cache_par.block_size)->default_value(default_block), "Block size")
    ("verbose,V", po::value<>(&verbose)->default_value(1)->implicit_value(1), "Verbose level")
    ("predictor", po::value<PredictorType>(&predictor_type), "Predictor type, default is DBSP\nPossible values: \n0) DBSP")
    ("prefetch", po::value<PrefetchPolicy>(&prefetch_policy),  "Prefetch policy, default level is Never\nPossible values: \n0) Never \n1) Always \n2) OnMiss")
    ("lookahead_range", po::value<>(&par.lookahead_range)->default_value(par.lookahead_range), "lookahead_range")
    ("max_support", po::value<>(&par.max_support)->default_value(par.max_support), "max_support")
    ("min_support", po::value<>(&par.min_support)->default_value(par.min_support), "min_support")
    ("pf_list_size", po::value<>(&par.pf_list_size)->default_value(par.pf_list_size), "pf_list_size")
    ("mtable_size", po::value<>(&par.mining_table_num_rows)->default_value(par.mining_table_num_rows), "mining_table_num_rows")
    ("rtable_size", po::value<>(&par.record_table_num_rows)->default_value(par.record_table_num_rows), "record_table_num_rows, default value for params_cases::OriginalPaperCase is 20e3")
    ("ptable_size", po::value<>(&par.prefetch_table_num_rows)->default_value(par.prefetch_table_num_rows),"prefetch_table_num_rows, default value for ""params_cases::OriginalPaperCase is 30e3")
    ("queue", po::value<>(&max_queue_size)->default_value(0), "Max depth of tasks queue")
    ("predictor_auto_config", po::bool_switch(&pr_auto_config), "True, used auto configeration params with predictor_size_bytes ")
    ("predictor_size_bytes", po::value<>(&pr_metadata_size_bytes), " Max prefetcher metadata size, bytes")
    ("latency", po::bool_switch(&latency), "Build predictor latency histogram")
    ("preload_trace", po::bool_switch(&preload_trace), "Preload input trace file into memory")
    ("sharded_predictor", po::bool_switch(&sharded_predictor), "Create predictor instance per shard");

    // clang-format on

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }

    if (sharded_predictor && num_shards) {
        par.mining_table_num_rows /= num_shards;
        par.prefetch_table_num_rows /= num_shards;
        par.record_table_num_rows /= num_shards;
    }

    std::cout << std::setw(30) << std::left << "Cache type : " << cache_type << std::endl;
    std::cout << std::setw(30) << std::left << "Cache size : " << cache_par.cache_size << std::endl;
    std::cout << std::setw(30) << std::left << "Page size : " << cache_par.page_size << std::endl;
    std::cout << std::setw(30) << std::left << "Block size : " << cache_par.block_size << std::endl;
    std::cout << std::setw(30) << std::left << "Num shards : " << num_shards << std::endl;
    std::cout << std::setw(30) << std::left << "Shard size : " << shard_size << std::endl;

    std::cout << std::setw(30) << std::left << "Predictor type : " << (size_t)predictor_type << std::endl;
    std::cout << std::setw(30) << std::left << "Prediction strategy type : " << (size_t)prefetch_policy << std::endl;

    std::unique_ptr<ShardedCache> c;
    try {
        c = std::make_unique<ShardedCache>(cache_type, prefetch_policy, predictor_type, num_shards, shard_size);

        c->Init(cache_par, par, sharded_predictor);

    } catch (std::runtime_error& er) {
        std::cout << "Initialization failed:\n" << er.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "Initialization failed!\n";
        return 1;
    }
    if (prefetch_policy != PrefetchPolicy::Never) {
        std::cout << std::setw(30) << std::left << "lookahead_range : " << par.lookahead_range << std::endl;
        std::cout << std::setw(30) << std::left << "max_support : " << par.max_support << std::endl;
        std::cout << std::setw(30) << std::left << "min_support : " << par.min_support << std::endl;
        std::cout << std::setw(30) << std::left << "confidence : " << par.confidence << std::endl;
        std::cout << std::setw(30) << std::left << "pf_list_size : " << par.pf_list_size << std::endl;
        std::cout << std::setw(30) << std::left << "mining_table_num_rows : " << par.mining_table_num_rows << std::endl;
        std::cout << std::setw(30) << std::left << "req_size_update_policy : " << par.req_size_update_policy << std::endl;
        std::cout << std::setw(30) << std::left << "limit_size_for_size_policy : " << par.limit_size_for_size_policy << std::endl;
        std::cout << std::setw(30) << std::left << "prefetch_table_num_rows : " << par.prefetch_table_num_rows << std::endl;
        std::cout << std::setw(30) << std::left << "record_table_num_rows : " << par.record_table_num_rows << std::endl;
        std::cout << std::setw(30) << std::left << "thread_count : " << par.thread_count << std::endl;
    }
    Worker worker;
    worker.Start();

#ifdef PREFETCH_ENABLE_MULTI_THREADED
    std::mutex mutex;
    std::condition_variable cv;
    max_queue_size = max_queue_size ? max_queue_size : (num_shards ? num_shards : 1);
#else
    if (max_queue_size > 1) {
        std::cout << "WARNING: parameter 'queue' (" << max_queue_size << ") is ignored\n";
    }
#endif

    std::atomic<size_t> hits = 0;
    std::atomic<size_t> misses = 0;
    std::atomic<size_t> total = 0;
    std::atomic<size_t> prefetched = 0;
    std::atomic<size_t> evicted_untouched = 0;
    std::atomic<size_t> submitted = 0;
    std::atomic<size_t> processed = 0;
    std::atomic<size_t> num_internal_requests = 0;
    std::map<size_t, size_t> latency_stat;

    const size_t block_size = cache_par.block_size;
    if (!(block_size != 0 && (block_size & (block_size - 1)) == 0)) {
        throw std::runtime_error(std::string("block_size=") + std::to_string(block_size) + std::string(" must be power of 2"));
    }
    
    TraceReader reader(input.c_str(), num_requests, skip, preload_trace);


    auto start = std::chrono::steady_clock::now();

    auto r = reader.read_next();
    for (size_t i = 0; Valid(r); ++i, r = reader.read_next()) {
        // r.alignToBlockSize(block_size);
        auto futures = c->Process(r);
        submitted += futures.size();

        auto sync_future_func = [&, r, loop = i](auto&& future) -> void {
            auto [hit, miss, p, e, l, num_req] = future.get();

            hits += hit.val;
            misses += miss.val;
            total += hit.val + miss.val;
            prefetched += p.val;
            evicted_untouched += e.val;
            num_internal_requests += num_req.val;

            {
#ifdef PREFETCH_ENABLE_MULTI_THREADED
                std::unique_lock<std::mutex> lock(mutex);
#endif
                processed++;
                if (latency) {
                    if (l.val > 1000)
                        l.val -= l.val % 1000;
                    else if (l.val > 100)
                        l.val -= l.val % 100;
                    else if (l.val > 10)
                        l.val -= l.val % 10;
                    ++latency_stat[l.val];
                }
#ifdef PREFETCH_ENABLE_MULTI_THREADED
                cv.notify_one();
#endif
            }

            // if (verbose > 1) {
            //     printf("Request {%08zu:%08zu} => hit %d, miss %d\n", r.start_addr_, r.size_bytes_, hit.val, miss.val);
            // } else if (verbose > 0 && !(processed.load() % 100)) {
            //     printf("\r%.2f %%", loop / float(num_of_req) * 100);
            // }
        };

#ifdef PREFETCH_ENABLE_MULTI_THREADED
        for (auto& f : futures) {
            auto res = worker.AddTask(false, sync_future_func, std::move(f));
            std::ignore = res;
        }

        {
            // This is not to overfeed async task queue
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&submitted, &processed, &max_queue_size]() -> bool {
                return (submitted - processed) <= max_queue_size;
            });
        }
#else
        for (auto& f : futures) {
            sync_future_func(std::move(f));
        }
#endif
    }

    worker.Stop();
    auto stop = std::chrono::steady_clock::now();

    std::cout << "\n\nResults:";
    std::cout.imbue(std::locale(std::locale(), new customseps));
    std::cout << "\nnum requests " << processed.load() + num_internal_requests.load() << ", hits " << hits.load() << ", misses " << misses.load() << ", total "
              << total.load() << ", ratio (in %) " << double(hits.load()) / total.load() * 100;
    std::cout << "\nnum prefetched " << prefetched.load() << ", evicted untouched " << evicted_untouched.load() << std::endl;
    std::cout << "Time elapsed: " << std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count() << std::endl;
    if (latency) {
        std::cout << "Latency (usec : occurances)" << std::endl;
        for (const auto& x : latency_stat)
            std::cout << x.first << " : " << x.second << std::endl;
    }
    return 0;
}