//
// Created by zhenyus on 2/17/19.
//

#include "lecar.h"

bool LeCaRCache::lookup(const SimpleRequest &req)
{

#ifdef EVICTION_LOGGING
    {
        const auto &_req = dynamic_cast<const AnnotatedRequest &>(req);
        current_t = req.seq;
        auto it = future_timestamps.find(req.id);
        if (it == future_timestamps.end()) {
            future_timestamps.insert({_req.id, _req.next_seq});
        } else {
            it->second = _req.next_seq;
        }
    }
#endif


    auto & key = req.id;

    auto it = size_map.find(key);
    if (it != size_map.end()) {
        auto it_recency = recency.right.find(key);
//        cout<<(it_recency == recency.right.end())<<endl;
        recency.right.replace_data(it_recency, req.seq);
        auto it_frequency = frequency.right.find(key);
        uint64_t f = it_frequency->second.first;
        uint64_t t = it_frequency->second.second;
        uint64_t k = it_frequency->first;
//        cout<<(it_frequency == frequency.right.end())<<endl;
        frequency.right.replace_data(it_frequency, make_pair(f + 1, req.seq));
        return true;
    }
    return false;
}

void LeCaRCache::admit(const SimpleRequest &req) {
    auto & key = req.id;
    auto & size = req.size;
    auto & t = req.seq;
    // object feasible to store?
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req.id, size);
        return;
    }

    auto it_h_lru = h_lru.right.find(key);
    if (it_h_lru != h_lru.right.end()) {
        //update reward
        w[0] *= exp(- learning_rate * pow(discount_rate, (t - it_h_lru->second.first)));
        double _sum = w[0] + w[1];
        w[0] /= _sum;
        w[1] /= _sum;

        h_lru.right.erase(it_h_lru);
        h_size_map.erase(key);
        h_lru_current_size -= size;
    }

    auto it_h_lfu = h_lfu.right.find(key);
    if (it_h_lfu != h_lfu.right.end()) {
        //update reward
        w[1] *= exp(- learning_rate * pow(discount_rate, (t - it_h_lfu->second.first)));
        double _sum = w[0] + w[1];
        w[0] /= _sum;
        w[1] /= _sum;

        h_lfu.right.erase(it_h_lfu);
        h_size_map.erase(key);
        h_lfu_current_size -= size;
//        cout<<"removing k: "<<key<<" size: "<<size<<endl;
//        cout<<"map size: "<<h_lfu.left.size()<<endl;
    }

    // admit new object
    _currentSize += size;
    recency.left.insert({t, key});
    frequency.left.insert({{1, t}, key});
    size_map.insert({key, size});

    // check eviction needed
    // use to order multiple evictions in the same t
    uint64_t counter = 0;
    while (_currentSize > _cacheSize) {
        evict(t, counter);
        ++counter;
    }
}

void LeCaRCache::evict(const uint64_t &t, uint64_t & counter) {
    double r = (double (rand())) / RAND_MAX;
    if (r < w[0]) {
        //lru policy
        auto it = recency.left.begin();
        auto key = it->second;
        auto it_size = size_map.find(key);
//        cout<<(it_size == size_map.end())<<endl;
        auto & size = it_size->second;
        //add new to h
#ifdef EVICTION_LOGGING
        {
            auto it = future_timestamps.find(key);
            unsigned int decision_qulity =
                    static_cast<double>(it->second - current_t) / (_cacheSize * 1e6 / byte_million_req);
            decision_qulity = min((unsigned int) 255, decision_qulity);
            eviction_qualities.emplace_back(decision_qulity);
            eviction_logic_timestamps.emplace_back(current_t / 65536);
        }
#endif
//        cout<<"before inserting t: "<<t<<" k: "<<key<<" size: "<<size<<" map size: "<<h_lfu.left.size()<<endl;
        auto tmp_it = h_lru.left.find(make_pair(t, counter));
//        cout<<(tmp_it == h_lfu.left.end())<<endl;
        h_lru.left.insert({{t, counter}, key});
        h_size_map.insert({key, size});
        h_lru_current_size += size;
        //remove new from c
//        cout<<"inserting k: "<<key<<" size: "<<size<<endl;
//        cout<<"map size: "<<h_lfu.left.size()<<endl;
        _currentSize -= size;
        recency.left.erase(it);
        frequency.right.erase(key);
        size_map.erase(key);
        //remove old from h
        while (h_lru_current_size > _cacheSize) {
            auto lru_it = h_lru.left.begin();
            auto & key = lru_it->second;
            auto & size = h_size_map.find(key)->second;

#ifdef EVICTION_LOGGING
            {
                auto it = future_timestamps.find(key);
                unsigned int decision_qulity =
                        static_cast<double>(it->second - current_t) / (_cacheSize * 1e6 / byte_million_req);
                decision_qulity = min((unsigned int) 255, decision_qulity);
                eviction_qualities.emplace_back(decision_qulity);
                eviction_logic_timestamps.emplace_back(current_t / 65536);
            }
#endif

            h_lru_current_size -= size;
            h_size_map.erase(key);
            h_lru.left.erase(lru_it);
        }
    } else {
        //lfu policy
        auto it = frequency.left.begin();
        auto key = it->second;
        auto it_size = size_map.find(key);
//        cout<<(it_size == size_map.end())<<endl;
        auto & size = it_size->second;
#ifdef EVICTION_LOGGING
        {
            auto it = future_timestamps.find(key);
            unsigned int decision_qulity =
                    static_cast<double>(it->second - current_t) / (_cacheSize * 1e6 / byte_million_req);
            decision_qulity = min((unsigned int) 255, decision_qulity);
            eviction_qualities.emplace_back(decision_qulity);
            eviction_logic_timestamps.emplace_back(current_t / 65536);
        }
#endif
        //add new to h
//        cout<<"before inserting t: "<<t<<" k: "<<key<<" size: "<<size<<" map size: "<<h_lfu.left.size()<<endl;
        auto tmp_it = h_lfu.left.find(make_pair(t, counter));
//        cout<<(tmp_it == h_lfu.left.end())<<endl;
        h_lfu.left.insert({{t, counter}, key});
        h_size_map.insert({key, size});
        h_lfu_current_size += size;
        //remove new from c
//        cout<<"inserting k: "<<key<<" size: "<<size<<endl;
//        cout<<"map size: "<<h_lfu.left.size()<<endl;
        _currentSize -= size;
        frequency.left.erase(it);
        recency.right.erase(key);
        size_map.erase(key);
        //remove old from h
        while (h_lfu_current_size > _cacheSize) {
            auto lfu_it = h_lfu.left.begin();
            auto & key = lfu_it->second;
            auto & size = h_size_map.find(key)->second;
#ifdef EVICTION_LOGGING
            {
                auto it = future_timestamps.find(key);
                unsigned int decision_qulity =
                        static_cast<double>(it->second - current_t) / (_cacheSize * 1e6 / byte_million_req);
                decision_qulity = min((unsigned int) 255, decision_qulity);
                eviction_qualities.emplace_back(decision_qulity);
                eviction_logic_timestamps.emplace_back(current_t / 65536);
            }
#endif
//            cout<<(lfu_it == h_lfu.left.end())<<endl;
            h_lfu_current_size -= size;
            h_size_map.erase(key);
            h_lfu.left.erase(lfu_it);
//            cout<<"removing k: "<<key<<" size: "<<size<<endl;
//            cout<<"map size: "<<h_lfu.left.size()<<endl;
        }
    }

}
