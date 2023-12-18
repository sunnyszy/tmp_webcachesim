#include <unordered_map>
#include <cassert>
#include "gd_variants.h"

#ifdef CDEBUG
#include <vector>
#include <algorithm>
#endif

using namespace std;

/*
  GD: greedy dual eviction (base class)
*/
bool GreedyDualBase::lookup(const SimpleRequest &req)
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

    auto & obj = req.id;
    auto it = _cacheMap.find(obj);
    if (it != _cacheMap.end()) {
        // log hit
        LOG("h", 0, obj.id, obj.size);
        hit(req);
        return true;
    }
    return false;
}

void GreedyDualBase::admit(const SimpleRequest &req)
{
#ifdef CDEBUG
    {
        DPRINTF("cache state: \n");
        vector<uint64_t> cache_state;
        for (auto &it: _cacheMap)
            cache_state.push_back(it.first.id);
        sort(cache_state.begin(), cache_state.end());
        for (auto &it: cache_state)
            DPRINTF("%lu\n", it);
    }
#endif

    const auto size = req.size;
    // object feasible to store?
    if (size >= _cacheSize) {
        LOG("L", _cacheSize, req.id, size);
        return;
    }
    auto & obj = req.id;
    _sizemap[obj] = size;
    // admit new object with new GF value
    long double ageVal = ageValue(req);
    LOG("a", ageVal, obj.id, obj.size);
    _cacheMap[obj] = _valueMap.emplace(ageVal, obj);
    _currentSize += size;
//    LOG("csize", _currentSize, 0, 0);
    // check eviction needed
    while (_currentSize > _cacheSize) {
        evict();
    }

#ifdef CDEBUG
    {
        DPRINTF("cache state: \n");
        vector<uint64_t> cache_state;
        for (auto &it: _cacheMap)
            cache_state.push_back(it.first.id);
        sort(cache_state.begin(), cache_state.end());
        for (auto &it: cache_state)
            DPRINTF("%lu\n", it);
    }
#endif
}

//void GreedyDualBase::evict(SimpleRequest& req)
//{
//    // evict the object match id, type, size of this request
//    CacheObject obj(req);
//    auto it = _cacheMap.find(obj);
//    if (it != _cacheMap.end()) {
//        auto lit = it->second;
//        CacheObject toDelObj = it->first;
//        LOG("e", lit->first, toDelObj.id, toDelObj.size);
//        _currentSize -= toDelObj.size;
//        _valueMap.erase(lit);
//        _cacheMap.erase(it);
//    }
//}

void GreedyDualBase::evict()
{
    // evict first list element (smallest value)
    if (_valueMap.size() > 0) {
        ValueMapIteratorType lit  = _valueMap.begin();
        if (lit == _valueMap.end()) {
            std::cerr << "underun: " << _currentSize << ' ' << _cacheSize << std::endl;
        }
        assert(lit != _valueMap.end()); // bug if this happens
        uint64_t toDelObj = lit->second;

#ifdef EVICTION_LOGGING
        {
            auto it = future_timestamps.find(toDelObj);
            unsigned int decision_qulity =
                    static_cast<double>(it->second - current_t) / (_cacheSize * 1e6 / byte_million_req);
            decision_qulity = min((unsigned int) 255, decision_qulity);
            eviction_qualities.emplace_back(decision_qulity);
            eviction_logic_timestamps.emplace_back(current_t / 65536);
        }
#endif

        LOG("e", lit->first, toDelObj.id, toDelObj.size);
        auto size = _sizemap[toDelObj];
        _currentSize -= size;
        _cacheMap.erase(toDelObj);
        _sizemap.erase(toDelObj);
//        LOG("csize", _currentSize, 0, 0);
        // update L
        _currentL = lit->first;
        _valueMap.erase(lit);
    }
}

long double GreedyDualBase::ageValue(const SimpleRequest& req)
{
    return _currentL + 1.0;
}

void GreedyDualBase::hit(const SimpleRequest& req)
{
    auto & obj = req.id;
    // get iterator for the old position
    auto it = _cacheMap.find(obj);
    assert(it != _cacheMap.end());
    uint64_t cachedObj = it->first;
    ValueMapIteratorType si = it->second;
    // update current req's value to hval:
    _valueMap.erase(si);
    long double hval = ageValue(req);
    it->second = _valueMap.emplace(hval, cachedObj);
}

///*
//  Greedy Dual Size policy
//*/
//long double GDSCache::ageValue(const SimpleRequest* req)
//{
//    const uint64_t size = req->getSize();
//    return _currentL + 1.0 / static_cast<double>(size);
//}
//
/*
  Greedy Dual Size Frequency policy
*/
bool GDSFCache::lookup(const SimpleRequest& req)
{
    bool hit = GreedyDualBase::lookup(req);
    auto & obj = req.id;
    if (!hit) {
        _reqsMap[obj] = 1; //reset bec. reqs_map not updated when element removed
    } else {
        _reqsMap[obj]++;
    }
    return hit;
}

long double GDSFCache::ageValue(const SimpleRequest& req)
{
    auto & obj = req.id;
    uint64_t & size = _sizemap[obj];
    assert(_sizemap.find(obj) != _sizemap.end());
    return _currentL + static_cast<double>(_reqsMap[obj]) / static_cast<double>(size);
}

/*
  LRU-K policy
*/
LRUKCache::LRUKCache()
    : GreedyDualBase(),
      _tk(2),
      _curTime(0)
{
}


bool LRUKCache::lookup(const SimpleRequest& req) {
    auto & obj = req.id;
    _curTime++;
    _refsMap[obj].push(_curTime);
    bool hit = GreedyDualBase::lookup(req);
    return hit;
}

//void LRUKCache::evict(SimpleRequest& req)
//{
//    auto & obj = req.id;
//    _refsMap.erase(obj); // delete LRU-K info
//    GreedyDualBase::evict(req);
//}

void LRUKCache::evict() {
    // evict first list element (smallest value)
    if (_valueMap.size() > 0) {
        ValueMapIteratorType lit = _valueMap.begin();
        if (lit == _valueMap.end()) {
            std::cerr << "underun: " << _currentSize << ' ' << _cacheSize << std::endl;
        }
        assert(lit != _valueMap.end()); // bug if this happens
        uint64_t obj = lit->second;
        _refsMap.erase(obj); // delete LRU-K info
        GreedyDualBase::evict();
    }
}

long double LRUKCache::ageValue(const SimpleRequest& req)
{
    auto & obj = req.id;
    long double newVal = 0.0L;
    if(_refsMap[obj].size() >= _tk) {
        newVal = _refsMap[obj].front();
        _refsMap[obj].pop();
    }
    //std::cerr << id << " " << _curTime << " " << _refsMap[id].size() << " " << newVal << " " << _currentL << std::endl;
    return newVal;
}

/*
  LFUDA
*/
bool LFUDACache::lookup(const SimpleRequest& req)
{
    bool hit = GreedyDualBase::lookup(req);
    auto & obj = req.id;
    if (!hit) {
        _reqsMap[obj] = 1; //reset bec. reqs_map not updated when element removed
    } else {
        _reqsMap[obj]++;
    }
    return hit;
}

long double LFUDACache::ageValue(const SimpleRequest& req)
{
    auto & obj = req.id;
    return _currentL + _reqsMap[obj];
}

/*
  LFU
*/
bool LFUCache::lookup(const SimpleRequest& req)
{
    bool hit = GreedyDualBase::lookup(req);
    auto & obj = req.id;
    if (!hit) {
        _reqsMap[obj] = 1; //reset bec. reqs_map not updated when element removed
    } else {
        _reqsMap[obj]++;
    }
    return hit;
}

long double LFUCache::ageValue(const SimpleRequest& req)
{
    auto & obj = req.id;
    return _reqsMap[obj];
}

