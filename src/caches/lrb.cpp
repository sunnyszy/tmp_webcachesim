//
// Created by zhenyus on 1/16/19.
//

#include "lrb.hpp"
#include <algorithm>
#include "utils.h"
#include <chrono>

using namespace chrono;
using namespace std;
using namespace lrb;

void LRBCache::train() {
    ++n_retrain;
    auto timeBegin = chrono::system_clock::now();
    if (booster) LGBM_BoosterFree(booster);
    // create training dataset
    DatasetHandle trainData;
    LGBM_DatasetCreateFromCSR(
            static_cast<void *>(training_data->indptr.data()),
            C_API_DTYPE_INT32,
            training_data->indices.data(),
            static_cast<void *>(training_data->data.data()),
            C_API_DTYPE_FLOAT64,
            training_data->indptr.size(),
            training_data->data.size(),
            n_feature,  //remove future t
            training_params,
            nullptr,
            &trainData);

    LGBM_DatasetSetField(trainData,
                         "label",
                         static_cast<void *>(training_data->labels.data()),
                         training_data->labels.size(),
                         C_API_DTYPE_FLOAT32);

    // init booster
    LGBM_BoosterCreate(trainData, training_params, &booster);
    // train
    for (int i = 0; i < stoi(training_params["num_iterations"]); i++) {
        int isFinished;
        LGBM_BoosterUpdateOneIter(booster, &isFinished);
        if (isFinished) {
            break;
        }
    }

    int64_t len;
    vector<double> result(training_data->indptr.size() - 1);
    LGBM_BoosterPredictForCSR(booster,
                              static_cast<void *>(training_data->indptr.data()),
                              C_API_DTYPE_INT32,
                              training_data->indices.data(),
                              static_cast<void *>(training_data->data.data()),
                              C_API_DTYPE_FLOAT64,
                              training_data->indptr.size(),
                              training_data->data.size(),
                              n_feature,  //remove future t
                              C_API_PREDICT_NORMAL,
                              0,
                              training_params,
                              &len,
                              result.data());


    double se = 0;
    for (int i = 0; i < result.size(); ++i) {
        auto diff = result[i] - training_data->labels[i];
        se += diff * diff;
    }
    training_loss = training_loss * 0.99 + se / batch_size * 0.01;

    LGBM_DatasetFree(trainData);
    training_time = 0.95 * training_time +
                    0.05 * chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now() - timeBegin).count();
}

void LRBCache::sample() {
    // start sampling once cache filled up
    auto rand_idx = _distribution(_generator);
    auto n_in = static_cast<uint32_t>(in_cache_metas.size());
    auto n_out = static_cast<uint32_t>(out_cache_metas.size());
    bernoulli_distribution distribution_from_in(static_cast<double>(n_in) / (n_in + n_out));
    auto is_from_in = distribution_from_in(_generator);
    if (is_from_in == true) {
        uint32_t pos = rand_idx % n_in;
        auto &meta = in_cache_metas[pos];
        meta.emplace_sample(current_seq);
    } else {
        uint32_t pos = rand_idx % n_out;
        auto &meta = out_cache_metas[pos];
        meta.emplace_sample(current_seq);
    }
}


void LRBCache::update_stat_periodic() {
//    uint64_t feature_overhead = 0;
//    uint64_t sample_overhead = 0;
//    for (auto &m: in_cache_metas) {
//        feature_overhead += m.feature_overhead();
//        sample_overhead += m.sample_overhead();
//    }
//    for (auto &m: out_cache_metas) {
//        feature_overhead += m.feature_overhead();
//        sample_overhead += m.sample_overhead();
//    }
    float percent_beyond;
    if (0 == obj_distribution[0] && 0 == obj_distribution[1]) {
        percent_beyond = 0;
    } else {
        percent_beyond = static_cast<float>(obj_distribution[1])/(obj_distribution[0] + obj_distribution[1]);
    }
    obj_distribution[0] = obj_distribution[1] = 0;
    segment_percent_beyond.emplace_back(percent_beyond);
    segment_n_retrain.emplace_back(n_retrain);
    segment_n_in.emplace_back(in_cache_metas.size());
    segment_n_out.emplace_back(out_cache_metas.size());

    float positive_example_ratio;
    if (0 == training_data_distribution[0] && 0 == training_data_distribution[1]) {
        positive_example_ratio = 0;
    } else {
        positive_example_ratio = static_cast<float>(training_data_distribution[1])/(training_data_distribution[0] + training_data_distribution[1]);
    }
    training_data_distribution[0] = training_data_distribution[1] = 0;
    segment_positive_example_ratio.emplace_back(positive_example_ratio);

    n_retrain = 0;
    cerr
            << "in/out metadata: " << in_cache_metas.size() << " / " << out_cache_metas.size() << endl
            //    cerr << "feature overhead: "<<feature_overhead<<endl;
            << "memory_window: " << memory_window << endl
//            << "percent_beyond: " << percent_beyond << endl
//            << "feature overhead per entry: " << static_cast<double>(feature_overhead) / key_map.size() << endl
//            //    cerr << "sample overhead: "<<sample_overhead<<endl;
//            << "sample overhead per entry: " << static_cast<double>(sample_overhead) / key_map.size() << endl
            << "n_training: " << training_data->labels.size() << endl
            //            << "training loss: " << training_loss << endl
            << "training_time: " << training_time << " ms" << endl
            << "inference_time: " << inference_time << " us" << endl;
    assert(in_cache_metas.size() + out_cache_metas.size() == key_map.size());
}


bool LRBCache::lookup(const SimpleRequest &req) {
    bool ret;
    ++current_seq;

#ifdef EVICTION_LOGGING
    {
        AnnotatedRequest *_req = (AnnotatedRequest *) &req;
        auto it = future_timestamps.find(_req->id);
        if (it == future_timestamps.end()) {
            future_timestamps.insert({_req->id, _req->next_seq});
        } else {
            it->second = _req->next_seq;
        }
    }
#endif
    forget();

    //first update the metadata: insert/update, which can trigger pending data.mature
    auto it = key_map.find(req.id);
    if (it != key_map.end()) {
        auto list_idx = it->second.list_idx;
        auto list_pos = it->second.list_pos;
        Meta &meta = list_idx ? out_cache_metas[list_pos] : in_cache_metas[list_pos];
        //update past timestamps
        assert(meta._key == req.id);
        uint64_t last_timestamp = meta._past_timestamp;
        uint64_t forget_timestamp = last_timestamp % memory_window;
        //if the key in out_metadata, it must also in forget table
        assert((!list_idx) ||
               (negative_candidate_queue->find(forget_timestamp) !=
                negative_candidate_queue->end()));
        //re-request
        if (!meta._sample_times.empty()) {
            //mature
            for (auto &sample_time: meta._sample_times) {
                //don't use label within the first forget window because the data is not static
                uint32_t future_distance = current_seq - sample_time;
                training_data->emplace_back(meta, sample_time, future_distance, meta._key);
                ++training_data_distribution[1];
            }
            //batch_size ~>= batch_size
            if (training_data->labels.size() >= batch_size) {
                train();
                training_data->clear();
            }
            meta._sample_times.clear();
            meta._sample_times.shrink_to_fit();
        }

#ifdef EVICTION_LOGGING
        if (!meta._eviction_sample_times.empty()) {
            //mature
            for (auto &sample_time: meta._eviction_sample_times) {
                //don't use label within the first forget window because the data is not static
                uint32_t future_distance = req.seq - sample_time;
                eviction_training_data->emplace_back(meta, sample_time, future_distance, meta._key);
                //training
                if (eviction_training_data->labels.size() == batch_size) {
                    eviction_training_data->clear();
                }
            }
            meta._eviction_sample_times.clear();
            meta._eviction_sample_times.shrink_to_fit();
        }
#endif

        //make this update after update training, otherwise the last timestamp will change
#ifdef EVICTION_LOGGING
        AnnotatedRequest *_req = (AnnotatedRequest *) &req;
        meta.update(current_seq, _req->next_seq);
#else
        meta.update(current_seq);
#endif
        if (list_idx) {
            negative_candidate_queue->erase(forget_timestamp);
            negative_candidate_queue->insert({current_seq % memory_window, req.id});
            assert(negative_candidate_queue->find(current_seq % memory_window) !=
                   negative_candidate_queue->end());
        } else {
            auto *p = dynamic_cast<InCacheMeta *>(&meta);
            p->p_last_request = in_cache_lru_queue.re_request(p->p_last_request);
        }
        //update negative_candidate_queue
        ret = !list_idx;
    } else {
        ret = false;
    }

    //sampling happens late to prevent immediate re-request
    if (is_sampling) {
        sample();
    }

    return ret;
}


void LRBCache::forget() {
    /*
     * forget happens exactly after the beginning of each time, without doing any other operations. For example, an
     * object is request at time 0 with memory window = 5, and will be forgotten exactly at the start of time 5.
     * */
    //remove item from forget table, which is not going to be affect from update
    auto it = negative_candidate_queue->find(current_seq % memory_window);
    if (it != negative_candidate_queue->end()) {
        auto forget_key = it->second;
        auto pos = key_map.find(forget_key)->second.list_pos;
        // Forget only happens at list 1
        assert(key_map.find(forget_key)->second.list_idx);
//        auto pos = meta_it->second.list_pos;
//        bool meta_id = meta_it->second.list_idx;
        auto &meta = out_cache_metas[pos];

        //timeout mature
        if (!meta._sample_times.empty()) {
            //mature
            //todo: potential to overfill
            uint32_t future_distance = memory_window * 2;
            for (auto &sample_time: meta._sample_times) {
                //don't use label within the first forget window because the data is not static
                training_data->emplace_back(meta, sample_time, future_distance, meta._key);
                ++training_data_distribution[0];
            }
            //batch_size ~>= batch_size
            if (training_data->labels.size() >= batch_size) {
                train();
                training_data->clear();
            }
            meta._sample_times.clear();
            meta._sample_times.shrink_to_fit();
        }

#ifdef EVICTION_LOGGING
        //timeout mature
        if (!meta._eviction_sample_times.empty()) {
            //mature
            //todo: potential to overfill
            uint32_t future_distance = memory_window * 2;
            for (auto &sample_time: meta._eviction_sample_times) {
                //don't use label within the first forget window because the data is not static
                eviction_training_data->emplace_back(meta, sample_time, future_distance, meta._key);
                //training
                if (eviction_training_data->labels.size() == batch_size) {
                    eviction_training_data->clear();
                }
            }
            meta._eviction_sample_times.clear();
            meta._eviction_sample_times.shrink_to_fit();
        }
#endif

        assert(meta._key == forget_key);
        remove_from_outcache_metas(meta, pos, forget_key);
    }
}

void LRBCache::admit(const SimpleRequest &req) {
    const uint64_t &size = req.size;
    // object feasible to store?
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req.id, size);
        return;
    }

    auto it = key_map.find(req.id);
    if (it == key_map.end()) {
        //fresh insert
        key_map.insert({req.id, {0, (uint32_t) in_cache_metas.size()}});
        auto lru_it = in_cache_lru_queue.request(req.id);
#ifdef EVICTION_LOGGING
        AnnotatedRequest *_req = (AnnotatedRequest *) &req;
        in_cache_metas.emplace_back(req.id, req.size, current_seq, req.extra_features, _req->next_seq, lru_it);
#else
        in_cache_metas.emplace_back(req.id, req.size, current_seq, req.extra_features, lru_it);
#endif
        _currentSize += size;
        //this must be a fresh insert
//        negative_candidate_queue.insert({(current_seq + memory_window)%memory_window, req.id});
        if (_currentSize <= _cacheSize)
            return;
    } else {
        //bring list 1 to list 0
        //first move meta data, then modify hash table
        uint32_t tail0_pos = in_cache_metas.size();
        auto &meta = out_cache_metas[it->second.list_pos];
        auto forget_timestamp = meta._past_timestamp % memory_window;
        negative_candidate_queue->erase(forget_timestamp);
        auto it_lru = in_cache_lru_queue.request(req.id);
        in_cache_metas.emplace_back(out_cache_metas[it->second.list_pos], it_lru);
        uint32_t tail1_pos = out_cache_metas.size() - 1;
        if (it->second.list_pos != tail1_pos) {
            //swap tail
            out_cache_metas[it->second.list_pos] = out_cache_metas[tail1_pos];
            key_map.find(out_cache_metas[tail1_pos]._key)->second.list_pos = it->second.list_pos;
        }
        out_cache_metas.pop_back();
        it->second = {0, tail0_pos};
        _currentSize += size;
    }
    if (_currentSize > _cacheSize) {
        //start sampling once cache is filled up
        is_sampling = true;
    }
    // check more eviction needed?
    while (_currentSize > _cacheSize) {
        evict();
    }
}


pair<uint64_t, uint32_t> LRBCache::rank() {
    {
        //if not trained yet, or in_cache_lru past memory window, use LRU
        auto &candidate_key = in_cache_lru_queue.dq.back();
        auto it = key_map.find(candidate_key);
        auto pos = it->second.list_pos;
        auto &meta = in_cache_metas[pos];
        if ((!booster) || (memory_window <= current_seq - meta._past_timestamp)) {
            //this use LRU force eviction, consider sampled a beyond boundary object
            if (booster) {
                ++obj_distribution[1];
            }
            return {meta._key, pos};
        }
    }


    int32_t indptr[sample_rate + 1];
    indptr[0] = 0;
    int32_t indices[sample_rate * n_feature];
    double data[sample_rate * n_feature];
    int32_t past_timestamps[sample_rate];
    uint32_t sizes[sample_rate];

    unordered_set<uint64_t> key_set;
    uint64_t keys[sample_rate];
    uint32_t poses[sample_rate];
    //next_past_timestamp, next_size = next_indptr - 1

    unsigned int idx_feature = 0;
    unsigned int idx_row = 0;

    auto n_new_sample = sample_rate - idx_row;
    while (idx_row != sample_rate) {
        uint32_t pos = _distribution(_generator) % in_cache_metas.size();
        auto &meta = in_cache_metas[pos];
        if (key_set.find(meta._key) != key_set.end()) {
            continue;
        } else {
            key_set.insert(meta._key);
        }
#ifdef EVICTION_LOGGING
        meta.emplace_eviction_sample(current_seq);
#endif

        keys[idx_row] = meta._key;
        poses[idx_row] = pos;
        //fill in past_interval
        indices[idx_feature] = 0;
        data[idx_feature++] = current_seq - meta._past_timestamp;
        past_timestamps[idx_row] = meta._past_timestamp;

        uint8_t j = 0;
        uint32_t this_past_distance = 0;
        uint8_t n_within = 0;
        if (meta._extra) {
            for (j = 0; j < meta._extra->_past_distance_idx && j < max_n_past_distances; ++j) {
                uint8_t past_distance_idx = (meta._extra->_past_distance_idx - 1 - j) % max_n_past_distances;
                uint32_t &past_distance = meta._extra->_past_distances[past_distance_idx];
                this_past_distance += past_distance;
                indices[idx_feature] = j + 1;
                data[idx_feature++] = past_distance;
                if (this_past_distance < memory_window) {
                    ++n_within;
                }
//                } else
//                    break;
            }
        }

        indices[idx_feature] = max_n_past_timestamps;
        data[idx_feature++] = meta._size;
        sizes[idx_row] = meta._size;

        for (uint k = 0; k < n_extra_fields; ++k) {
            indices[idx_feature] = max_n_past_timestamps + k + 1;
            data[idx_feature++] = meta._extra_features[k];
        }

        indices[idx_feature] = max_n_past_timestamps + n_extra_fields + 1;
        data[idx_feature++] = n_within;

        for (uint8_t k = 0; k < n_edc_feature; ++k) {
            indices[idx_feature] = max_n_past_timestamps + n_extra_fields + 2 + k;
            uint32_t _distance_idx = min(uint32_t(current_seq - meta._past_timestamp) / edc_windows[k],
                                         max_hash_edc_idx);
            if (meta._extra)
                data[idx_feature++] = meta._extra->_edc[k] * hash_edc[_distance_idx];
            else
                data[idx_feature++] = hash_edc[_distance_idx];
        }
        //remove future t
        indptr[++idx_row] = idx_feature;
    }

    int64_t len;
    double scores[sample_rate];
    system_clock::time_point timeBegin;
    //sample to measure inference time
    if (!(current_seq % 10000))
        timeBegin = chrono::system_clock::now();
    LGBM_BoosterPredictForCSR(booster,
                              static_cast<void *>(indptr),
                              C_API_DTYPE_INT32,
                              indices,
                              static_cast<void *>(data),
                              C_API_DTYPE_FLOAT64,
                              idx_row + 1,
                              idx_feature,
                              n_feature,  //remove future t
                              C_API_PREDICT_NORMAL,
                              0,
                              inference_params,
                              &len,
                              scores);
    if (!(current_seq % 10000))
        inference_time = 0.95 * inference_time +
                         0.05 *
                         chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now() - timeBegin).count();
//    for (int i = 0; i < n_sample; ++i)
//        result[i] -= (t - past_timestamps[i]);
    for (int i = sample_rate - n_new_sample; i < sample_rate; ++i) {
        //only monitor at the end of change interval
        if (scores[i] >= log1p(memory_window)) {
            ++obj_distribution[1];
        } else {
            ++obj_distribution[0];
        }
    }

    if (objective == object_miss_ratio) {
        for (uint32_t i = 0; i < sample_rate; ++i)
            scores[i] *= sizes[i];
    }

    vector<int> index(sample_rate, 0);
    for (int i = 0; i < index.size(); ++i) {
        index[i] = i;
    }

    sort(index.begin(), index.end(),
         [&](const int &a, const int &b) {
             return (scores[a] > scores[b]);
         }
    );

#ifdef EVICTION_LOGGING
    {
        if (start_train_logging) {
//            training_and_prediction_logic_timestamps.emplace_back(current_seq / 65536);
            for (int i = 0; i < sample_rate; ++i) {
                int current_idx = indptr[i];
                for (int p = 0; p < n_feature; ++p) {
                    if (p == indices[current_idx]) {
                        trainings_and_predictions.emplace_back(data[current_idx]);
                        if (current_idx + 1 < indptr[i + 1])
                            ++current_idx;
                    } else
                        trainings_and_predictions.emplace_back(NAN);
                }
                uint32_t future_interval = future_timestamps.find(keys[i])->second - current_seq;
                future_interval = min(2 * memory_window, future_interval);
                trainings_and_predictions.emplace_back(future_interval);
                trainings_and_predictions.emplace_back(scores[i]);
                //trainings_and_predictions.emplace_back(result[i]);
                trainings_and_predictions.emplace_back(current_seq);
                trainings_and_predictions.emplace_back(1);
                trainings_and_predictions.emplace_back(keys[i]);
            }
        }
    }
#endif

    return {keys[index[0]], poses[index[0]]};
}

void LRBCache::evict() {
    auto epair = rank();
    uint64_t &key = epair.first;
    uint32_t &old_pos = epair.second;

#ifdef EVICTION_LOGGING
    {
        auto it = future_timestamps.find(key);
        unsigned int decision_qulity =
                static_cast<double>(it->second - current_seq) / (_cacheSize * 1e6 / byte_million_req);
        decision_qulity = min((unsigned int) 255, decision_qulity);
        eviction_qualities.emplace_back(decision_qulity);
        eviction_logic_timestamps.emplace_back(current_seq / 65536);
    }
#endif

    auto &meta = in_cache_metas[old_pos];
    if (memory_window <= current_seq - meta._past_timestamp) {
        //must be the tail of lru
        if (!meta._sample_times.empty()) {
            //mature
            uint32_t future_distance = current_seq - meta._past_timestamp + memory_window;
            for (auto &sample_time: meta._sample_times) {
                //don't use label within the first forget window because the data is not static
                training_data->emplace_back(meta, sample_time, future_distance, meta._key);
                ++training_data_distribution[0];
            }
            //batch_size ~>= batch_size
            if (training_data->labels.size() >= batch_size) {
                train();
                training_data->clear();
            }
            meta._sample_times.clear();
            meta._sample_times.shrink_to_fit();
        }

#ifdef EVICTION_LOGGING
        //must be the tail of lru
        if (!meta._eviction_sample_times.empty()) {
            //mature
            uint32_t future_distance = current_seq - meta._past_timestamp + memory_window;
            for (auto &sample_time: meta._eviction_sample_times) {
                //don't use label within the first forget window because the data is not static
                eviction_training_data->emplace_back(meta, sample_time, future_distance, meta._key);
                //training
                if (eviction_training_data->labels.size() == batch_size) {
                    eviction_training_data->clear();
                }
            }
            meta._eviction_sample_times.clear();
            meta._eviction_sample_times.shrink_to_fit();
        }
#endif

        in_cache_lru_queue.dq.erase(meta.p_last_request);
        meta.p_last_request = in_cache_lru_queue.dq.end();
        //above is suppose to be below, but to make sure the action is correct
//        in_cache_lru_queue.dq.pop_back();
        meta.free();
        _currentSize -= meta._size;
        key_map.erase(key);

        uint32_t activate_tail_idx = in_cache_metas.size() - 1;
        if (old_pos != activate_tail_idx) {
            //move tail
            in_cache_metas[old_pos] = in_cache_metas[activate_tail_idx];
            key_map.find(in_cache_metas[activate_tail_idx]._key)->second.list_pos = old_pos;
        }
        in_cache_metas.pop_back();
        ++n_force_eviction;
    } else {
        //bring list 0 to list 1
        in_cache_lru_queue.dq.erase(meta.p_last_request);
        meta.p_last_request = in_cache_lru_queue.dq.end();
        _currentSize -= meta._size;
        negative_candidate_queue->insert({meta._past_timestamp % memory_window, meta._key});

        uint32_t new_pos = out_cache_metas.size();
        out_cache_metas.emplace_back(in_cache_metas[old_pos]);
        uint32_t activate_tail_idx = in_cache_metas.size() - 1;
        if (old_pos != activate_tail_idx) {
            //move tail
            in_cache_metas[old_pos] = in_cache_metas[activate_tail_idx];
            key_map.find(in_cache_metas[activate_tail_idx]._key)->second.list_pos = old_pos;
        }
        in_cache_metas.pop_back();
        key_map.find(key)->second = {1, new_pos};
    }
}

void LRBCache::remove_from_outcache_metas(Meta &meta, unsigned int &pos, const uint64_t &key) {
    //free the actual content
    meta.free();
    //TODO: can add a function to delete from a queue with (key, pos)
    //evict
    uint32_t tail_pos = out_cache_metas.size() - 1;
    if (pos != tail_pos) {
        //swap tail
        out_cache_metas[pos] = out_cache_metas[tail_pos];
        key_map.find(out_cache_metas[tail_pos]._key)->second.list_pos = pos;
    }
    out_cache_metas.pop_back();
    key_map.erase(key);
    negative_candidate_queue->erase(current_seq % memory_window);
}

