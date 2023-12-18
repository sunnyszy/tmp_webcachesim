//
// Created by Zhenyu Song on 10/30/18.
//

#include "simulation.h"
#include "annotate.h"
#include "trace_sanity_check.h"
#include "simulation_tinylfu.h"
#include <sstream>
#include "utils.h"
#include "rss.h"
#include <cstdint>
#include <unordered_map>
#include <numeric>
#include "bsoncxx/builder/basic/document.hpp"
#include "bsoncxx/json.hpp"

using namespace std;
using namespace chrono;
using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::sub_array;


FrameWork::FrameWork(const string &trace_file, const string &cache_type, const uint64_t &cache_size,
                     map<string, string> &params) {
    _trace_file = trace_file;
    _cache_type = cache_type;
    _cache_size = cache_size;
    is_offline = offline_algorithms.count(_cache_type);

    for (auto it = params.cbegin(); it != params.cend();) {
        if (it->first == "uni_size") {
            uni_size = static_cast<bool>(stoi(it->second));
            it = params.erase(it);
        } else if (it->first == "is_metadata_in_cache_size") {
            is_metadata_in_cache_size = static_cast<bool>(stoi(it->second));
#ifdef EVICTION_LOGGING
            if (true == is_metadata_in_cache_size) {
                throw invalid_argument(
                        "error: set is_metadata_in_cache_size while EVICTION_LOGGING. Must not consider metadata overhead");
            }
#endif
            it = params.erase(it);
        } else if (it->first == "bloom_filter") {
            bloom_filter = static_cast<bool>(stoi(it->second));
            it = params.erase(it);
        } else if (it->first == "segment_window") {
            segment_window = stoull((it->second));
            ++it;
        } else if (it->first == "n_extra_fields") {
            n_extra_fields = stoi((it->second));
            ++it;
        } else if (it->first == "real_time_segment_window") {
            real_time_segment_window = stoull((it->second));
            it = params.erase(it);
        } else if (it->first == "n_early_stop") {
            n_early_stop = stoll((it->second));
            ++it;
        } else if (it->first == "seq_start") {
            seq_start = stoll((it->second));
            ++it;
        } else {
            ++it;
        }
    }
#ifdef EVICTION_LOGGING
    //logging eviction requires next_seq information
    is_offline = true;
#endif

    //trace_file related init
    if (is_offline) {
        annotate(_trace_file, n_extra_fields);
    }

    if (is_offline) {
        _trace_file = _trace_file + ".ant";
    }
    infile.open(_trace_file);
    if (!infile) {
        cerr << "Exception opening/reading file " << _trace_file << endl;
        exit(-1);
    }

    //set cache_type related
    // create cache
    webcache = move(Cache::create_unique(cache_type));
    if (webcache == nullptr) throw runtime_error("Error: cache type " + cache_type + " not implemented");
    webcache->setSize(cache_size);
    webcache->init_with_params(params);

    adjust_real_time_offset();
    extra_features = vector<uint16_t>(n_extra_fields);
}

void FrameWork::adjust_real_time_offset() {
    // Zhenyu: not assume t start from any constant, so need to compute the first window
    if (is_offline) {
        infile >> next_seq >> t;
    } else {
        infile >> t;
    }
    time_window_end =
            real_time_segment_window * (t / real_time_segment_window + (t % real_time_segment_window != 0));
    infile.clear();
    infile.seekg(0, ios::beg);
}


void FrameWork::update_real_time_stats() {
    rt_seg_byte_miss.emplace_back(rt_byte_miss);
    rt_seg_byte_req.emplace_back(rt_byte_req);
    rt_seg_object_miss.emplace_back(rt_obj_miss);
    rt_seg_object_req.emplace_back(rt_obj_req);
    rt_byte_miss = rt_obj_miss = rt_byte_req = rt_obj_req = 0;
    //real time only read rss info
    auto metadata_overhead = get_rss();
    rt_seg_rss.emplace_back(metadata_overhead);
    time_window_end += real_time_segment_window;
}

void FrameWork::update_stats() {
    auto _t_now = chrono::system_clock::now();
    cerr << "\nseq: " << seq << endl
         << "cache size: " << webcache->_currentSize << "/" << webcache->_cacheSize
         << " (" << ((double) webcache->_currentSize) / webcache->_cacheSize << ")" << endl
         << "delta t: " << chrono::duration_cast<std::chrono::milliseconds>(_t_now - t_now).count() / 1000.
         << endl;
    t_now = _t_now;
    cerr << "segment bmr: " << double(byte_miss) / byte_req << endl;
    seg_byte_miss.emplace_back(byte_miss);
    seg_byte_req.emplace_back(byte_req);
    seg_object_miss.emplace_back(obj_miss);
    seg_object_req.emplace_back(obj_req);
    seg_byte_in_cache.emplace_back(webcache->_currentSize);
    byte_miss = obj_miss = byte_req = obj_req = 0;
    //reduce cache size by metadata
    auto metadata_overhead = get_rss();
    seg_rss.emplace_back(metadata_overhead);
    if (is_metadata_in_cache_size) {
        webcache->setSize(_cache_size - metadata_overhead);
    }
    cerr << "rss: " << metadata_overhead << endl;
    webcache->update_stat_periodic();
}


bsoncxx::builder::basic::document FrameWork::simulate() {
    cerr << "simulating" << endl;
    unordered_map<uint64_t, uint32_t> future_timestamps;
    vector<uint8_t> eviction_qualities;
    vector<uint16_t> eviction_logic_timestamps;
    if (bloom_filter) {
        filter = new AkamaiBloomFilter;
    }

    SimpleRequest *req;
    if (is_offline)
        req = new AnnotatedRequest(0, 0, 0, 0, 0);
    else
        req = new SimpleRequest(0, 0, 0);
    t_now = system_clock::now();

    int64_t seq_start_counter = 0;
    while (true) {
        if (is_offline) {
            if (!(infile >> next_seq >> t >> id >> size))
                break;
        } else {
            if (!(infile >> t >> id >> size))
                break;
        }

        if (seq_start_counter++ < seq_start) {
            continue;
        }
        if (seq == n_early_stop)
            break;

        for (int i = 0; i < n_extra_fields; ++i)
            infile >> extra_features[i];
        if (uni_size)
            size = 1;

        while (t >= time_window_end) {
            update_real_time_stats();
        }
        if (seq && !(seq % segment_window)) {
            update_stats();
        }

        update_metric_req(byte_req, obj_req, size);
        update_metric_req(rt_byte_req, rt_obj_req, size)

        if (is_offline)
            dynamic_cast<AnnotatedRequest *>(req)->reinit(seq, id, size, next_seq, &extra_features);
        else
            req->reinit(seq, id, size, &extra_features);

        bool is_admitting = true;
        if (true == bloom_filter) {
            bool exist_in_cache = webcache->exist(req->id);
            //in cache object, not consider bloom_filter
            if (false == exist_in_cache) {
                is_admitting = filter->exist_or_insert(id);
            }
        }
        if (is_admitting) {
            bool is_hit = webcache->lookup(*req);
            if (!is_hit) {
                update_metric_req(byte_miss, obj_miss, size);
                update_metric_req(rt_byte_miss, rt_obj_miss, size)
                webcache->admit(*req);
            }
        } else {
            update_metric_req(byte_miss, obj_miss, size);
            update_metric_req(rt_byte_miss, rt_obj_miss, size)
        }

        ++seq;
    }
    delete req;
    //for the residue segment of trace
    update_real_time_stats();
    update_stats();
    infile.close();

    return simulation_results();
}


bsoncxx::builder::basic::document FrameWork::simulation_results() {
    bsoncxx::builder::basic::document value_builder{};
    value_builder.append(kvp("no_warmup_byte_miss_ratio",
                             accumulate<vector<int64_t>::const_iterator, double>(seg_byte_miss.begin(),
                                                                                 seg_byte_miss.end(), 0) /
                             accumulate<vector<int64_t>::const_iterator, double>(seg_byte_req.begin(),
                                                                                 seg_byte_req.end(), 0)
    ));
//    value_builder.append(kvp("byte_miss_cache", byte_miss_cache));
//    value_builder.append(kvp("byte_miss_filter", byte_miss_filter));
    value_builder.append(kvp("segment_byte_miss", [this](sub_array child) {
        for (const auto &element : seg_byte_miss)
            child.append(element);
    }));
    value_builder.append(kvp("segment_byte_req", [this](sub_array child) {
        for (const auto &element : seg_byte_req)
            child.append(element);
    }));
    value_builder.append(kvp("segment_object_miss", [this](sub_array child) {
        for (const auto &element : seg_object_miss)
            child.append(element);
    }));
    value_builder.append(kvp("segment_object_req", [this](sub_array child) {
        for (const auto &element : seg_object_req)
            child.append(element);
    }));
    value_builder.append(kvp("segment_rss", [this](sub_array child) {
        for (const auto &element : seg_rss)
            child.append(element);
    }));
    value_builder.append(kvp("segment_byte_in_cache", [this](sub_array child) {
        for (const auto &element : seg_byte_in_cache)
            child.append(element);
    }));

    value_builder.append(kvp("real_time_segment_byte_miss", [this](sub_array child) {
        for (const auto &element : rt_seg_byte_miss)
            child.append(element);
    }));
    value_builder.append(kvp("real_time_segment_byte_req", [this](sub_array child) {
        for (const auto &element : rt_seg_byte_req)
            child.append(element);
    }));
    
    value_builder.append(kvp("real_time_segment_object_miss", [this](sub_array child) {
        for (const auto &element : rt_seg_object_miss)
            child.append(element);
    }));
    value_builder.append(kvp("real_time_segment_object_req", [this](sub_array child) {
        for (const auto &element : rt_seg_object_req)
            child.append(element);
    }));
    
    value_builder.append(kvp("real_time_segment_rss", [this](sub_array child) {
        for (const auto &element : rt_seg_rss)
            child.append(element);
    }));
    

    webcache->update_stat(value_builder);
    return value_builder;
}



bsoncxx::builder::basic::document _simulation(string trace_file, string cache_type, uint64_t cache_size,
                                              map<string, string> params) {
    FrameWork frame_work(trace_file, cache_type, cache_size, params);
    auto res = frame_work.simulate();
    return res;
}

bsoncxx::builder::basic::document simulation(string trace_file, string cache_type,
                                             uint64_t cache_size, map<string, string> params) {
    int n_extra_fields = get_n_fields(trace_file) - 3;
    params["n_extra_fields"] = to_string(n_extra_fields);

    bool enable_trace_format_check = true;
    if (params.find("enable_trace_format_check") != params.end()) {
        enable_trace_format_check = stoi(params.find("enable_trace_format_check")->second);
    }

    if (true == enable_trace_format_check) {
        auto if_pass = trace_sanity_check(trace_file, params);
        if (true == if_pass) {
            cerr << "pass sanity check" << endl;
        } else {
            throw std::runtime_error("fail sanity check");
        }
    }

    if (cache_type == "Adaptive-TinyLFU")
        return _simulation_tinylfu(trace_file, cache_type, cache_size, params);
    else
        return _simulation(trace_file, cache_type, cache_size, params);
}
