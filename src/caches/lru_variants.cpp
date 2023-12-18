#include <unordered_map>
#include <random>
#include <cmath>
#include <cassert>
#include "lru_variants.h"
#include "random_helper.h"

// golden section search helpers
#define SHFT2(a,b,c) (a)=(b);(b)=(c);
#define SHFT3(a,b,c,d) (a)=(b);(b)=(c);(c)=(d);

// math model below can be directly copiedx
// static inline double oP1(double T, double l, double p) {
static inline double oP1(double T, double l, double p) {
    return (l * p * T * (840.0 + 60.0 * l * T + 20.0 * l*l * T*T + l*l*l * T*T*T));
}

static inline double oP2(double T, double l, double p) {
    return (840.0 + 120.0 * l * (-3.0 + 7.0 * p) * T + 60.0 * l*l * (1.0 + p) * T*T + 4.0 * l*l*l * (-1.0 + 5.0 * p) * T*T*T + l*l*l*l * p * T*T*T*T);
}

/*
  LRU: Least Recently Used eviction
*/
bool LRUCache::lookup(const SimpleRequest &req)
{


#ifdef EVICTION_LOGGING
    {
        const auto &_req = dynamic_cast<const AnnotatedRequest &>(req);
        current_t = req.seq;

        if (_cacheMap.find(req.id) != _cacheMap.end()) {
            //hit
            auto it = last_timestamps.find(req.id);
            unsigned int hit =
                    static_cast<double>(current_t - it->second) / (_cacheSize * 1e6 / byte_million_req);
            hit = min((unsigned int) 255, hit);
            hits.emplace_back(hit);
            hit_timestamps.emplace_back(current_t / 65536);
        }

        auto it = last_timestamps.find(req.id);
        if (it == last_timestamps.end()) {
            last_timestamps.insert({_req.id, current_t});
        } else {
            it->second = current_t;
        }


        it = future_timestamps.find(req.id);
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
        auto & size = _size_map[obj];
        LOG("h", 0, obj.id, obj.size);
        hit(it, size);
        return true;
    }
    return false;
}

void LRUCache::admit(const SimpleRequest &req)
{
    const uint64_t size = req.size;
    // object feasible to store?
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req.id, size);
        return;
    }
    // check eviction needed
    while (_currentSize + size > _cacheSize) {
        evict();
    }
    // admit new object
    auto & obj = req.id;
    _cacheList.push_front(obj);
    _cacheMap[obj] = _cacheList.begin();
    _currentSize += size;
    _size_map[obj] = size;
    LOG("a", _currentSize, obj.id, obj.size);
}

bool InfCache::lookup(const SimpleRequest &req)
{
    return _cacheMap.find(req.id) != _cacheMap.end();
}

void InfCache::admit(const SimpleRequest &req)
{
    // admit new object
    _cacheMap.insert(req.id);
}

void LRUCache::evict(const int64_t& obj)
{
    auto it = _cacheMap.find(obj);
    if (it != _cacheMap.end()) {
        ListIteratorType lit = it->second;
        LOG("e", _currentSize, obj.id, obj.size);
        auto & size = _size_map[obj];
        _currentSize -= size;
        _size_map.erase(obj);
        _cacheMap.erase(obj);
        _cacheList.erase(lit);
    }
}

void LRUCache::evict()
{
    // evict least popular (i.e. last element)
    if (_cacheList.size() > 0) {
        ListIteratorType lit = _cacheList.end();
        lit--;
        uint64_t obj = *lit;


#ifdef EVICTION_LOGGING
        {
            auto it = future_timestamps.find(obj);
            unsigned int decision_qulity =
                    static_cast<double>(it->second - current_t) / (_cacheSize * 1e6 / byte_million_req);
            decision_qulity = min((unsigned int) 255, decision_qulity);
            eviction_qualities.emplace_back(decision_qulity);
            eviction_logic_timestamps.emplace_back(current_t / 65536);
        }
#endif

        LOG("e", _currentSize, obj.id, obj.size);
        auto & size = _size_map[obj];
        _currentSize -= size;
        _size_map.erase(obj);
        _cacheMap.erase(obj);
        _cacheList.erase(lit);
    }
}

void LRUCache::hit(lruCacheMapType::const_iterator it, uint64_t size)
{
    _cacheList.splice(_cacheList.begin(), _cacheList, it->second);
}

SimpleRequest LRUCache::evict_return() {
    // evict least popular (i.e. last element)
    auto lit = _cacheList.end();
    lit--;
    uint64_t obj = *lit;
    auto size = _size_map[obj];
    LOG("e", _currentSize, obj, size);
    SimpleRequest req(obj, size);
    _currentSize -= size;
    _size_map.erase(obj);
    _cacheMap.erase(obj);
    _cacheList.erase(lit);
    return req;
}

bool LRUCache::exist(const int64_t &key) {
    return _cacheMap.find(key) != _cacheMap.end();
}

/*
  FIFO: First-In First-Out eviction
*/
void FIFOCache::hit(lruCacheMapType::const_iterator it, uint64_t size)
{
}

//
///*
//  FilterCache (admit only after N requests)
//*/
//FilterCache::FilterCache()
//    : LRUCache(),
//      _nParam(2)
//{
//}
//
//void FilterCache::setPar(std::string parName, std::string parValue) {
//    if(parName.compare("n") == 0) {
//        const uint64_t n = std::stoull(parValue);
//        assert(n>0);
//        _nParam = n;
//    } else {
//        std::cerr << "unrecognized parameter: " << parName << std::endl;
//    }
//}
//
//
//bool FilterCache::lookup(const SimpleRequest& req)
//{
//    CacheObject obj(req);
//    _filter[obj]++;
//    return LRUCache::lookup(req);
//}
//
//void FilterCache::admit(const SimpleRequest& req)
//{
//    CacheObject obj(req);
//    if (_filter[obj] <= _nParam) {
//        return;
//    }
//    LRUCache::admit(req);
//}

AdaptSizeCache::AdaptSizeCache()
        : LRUCache()
        , _cParam(1 << 15)
        , statSize(0)
        , _maxIterations(15)
        , _reconfiguration_interval(500000)
        , _nextReconfiguration(_reconfiguration_interval)
{
    _gss_v=1.0-gss_r; // golden section search book parameters
}

void AdaptSizeCache::setPar(std::string parName, std::string parValue) {
    if(parName.compare("t") == 0) {
        const uint64_t t = stoull(parValue);
        assert(t>1);
        _reconfiguration_interval = t;
    } else if(parName.compare("i") == 0) {
        const uint64_t i = stoull(parValue);
        assert(i>1);
        _maxIterations = i;
    } else {
        std::cerr << "unrecognized parameter: " << parName << std::endl;
    }
}

bool AdaptSizeCache::lookup(const SimpleRequest &req)
{
    reconfigure();

    uint64_t tmpCacheObject0 = req.id;
    auto size = req.size;
    if(_intervalMetadata.count(tmpCacheObject0)==0
       && _longTermMetadata.count(tmpCacheObject0)==0) {
        // new object
        statSize += size;
    }
    // the else block is not necessary as webcachesim treats an object
    // with size changed as a new object
    /**
	} else {
        // keep track of changing object sizes
        if(_intervalMetadata.count(id)>0
        && _intervalMetadata[id].size != req.size()) {
        // outdated size info in _intervalMetadata
        statSize -= _intervalMetadata[id].size;
        statSize += req.size();
        }
        if(_longTermMetadata.count(id)>0 && _longTermMetadata[id].size != req.size()) {
        // outdated size info in ewma
        statSize -= _longTermMetadata[id].size;
        statSize += req.size();
        }
	}
    */

    // record stats
    auto& info = _intervalMetadata[tmpCacheObject0];
    info.requestCount += 1.0;
    info.objSize = size;

    return LRUCache::lookup(req);
}

void AdaptSizeCache::admit(const SimpleRequest &req)
{
    double roll = _uniform_real_distribution(globalGenerator);
    double admitProb = std::exp(-1.0 * double(req.size)/_cParam);

    if(roll < admitProb)
        LRUCache::admit(req);
}

void AdaptSizeCache::reconfigure() {
    --_nextReconfiguration;
    if (_nextReconfiguration > 0) {
        return;
    } else if(statSize <= getSize()*3) {
        // not enough data has been gathered
        _nextReconfiguration+=10000;
        return;
    } else {
        _nextReconfiguration = _reconfiguration_interval;
    }

    // smooth stats for objects
    for(auto it = _longTermMetadata.begin();
        it != _longTermMetadata.end();
        it++) {
        it->second.requestCount *= EWMA_DECAY;
    }

    // persist intervalinfo in _longTermMetadata
    for (auto it = _intervalMetadata.begin();
         it != _intervalMetadata.end();
         it++) {
        auto ewmaIt = _longTermMetadata.find(it->first);
        if(ewmaIt != _longTermMetadata.end()) {
            ewmaIt->second.requestCount += (1. - EWMA_DECAY)
                                           * it->second.requestCount;
            ewmaIt->second.objSize = it->second.objSize;
        } else {
            _longTermMetadata.insert(*it);
        }
    }
    _intervalMetadata.clear();

    // copy stats into vector for better alignment
    // and delete small values
    _alignedReqCount.clear();
    _alignedObjSize.clear();
    double totalReqCount = 0.0;
    uint64_t totalObjSize = 0.0;
    for(auto it = _longTermMetadata.begin();
        it != _longTermMetadata.end();
        /*none*/) {
        if(it->second.requestCount < 0.1) {
            // delete from stats
            statSize -= it->second.objSize;
            it = _longTermMetadata.erase(it);
        } else {
            _alignedReqCount.push_back(it->second.requestCount);
            totalReqCount += it->second.requestCount;
            _alignedObjSize.push_back(it->second.objSize);
            totalObjSize += it->second.objSize;
            ++it;
        }
    }

    std::cerr << "Reconfiguring over " << _longTermMetadata.size()
              << " objects - log2 total size " << std::log2(totalObjSize)
              << " log2 statsize " << std::log2(statSize) << std::endl;

    // assert(totalObjSize==statSize);
    //
    // if(totalObjSize > cacheSize*2) {
    //
    // model hit rate and choose best admission parameter, c
    // search for best parameter on log2 scale of c, between min=x0 and max=x3
    // x1 and x2 bracket our current estimate of the optimal parameter range
    // |x0 -- x1 -- x2 -- x3|
    double x0 = 0;
    double x1 = std::log2(getSize());
    double x2 = x1;
    double x3 = x1;

    double bestHitRate = 0.0;
    // course_granular grid search
    for(int i=2; i<x3; i+=4) {
        const double next_log2c = i; // 1.0 * (i+1) / NUM_PARAMETER_POINTS;
        const double hitRate = modelHitRate(next_log2c);
        // printf("Model param (%f) : ohr (%f)\n",
        // 	next_log2c,hitRate/totalReqRate);

        if(hitRate > bestHitRate) {
            bestHitRate = hitRate;
            x1 = next_log2c;
        }
    }

    double h1 = bestHitRate;
    double h2;
    //prepare golden section search into larger segment
    if(x3-x1 > x1-x0) {
        // above x1 is larger segment
        x2 = x1+_gss_v*(x3-x1);
        h2 = modelHitRate(x2);
    } else {
        // below x1 is larger segment
        x2 = x1;
        h2 = h1;
        x1 = x0+_gss_v*(x1-x0);
        h1 = modelHitRate(x1);
    }
    assert(x1<x2);

    uint64_t curIterations=0;
    // use termination condition from [Numerical recipes in C]
    while(curIterations++<_maxIterations
          && fabs(x3-x0)>tol*(fabs(x1)+fabs(x2))) {
        //NAN check
        if((h1!=h1) || (h2!=h2))
            break;
        // printf("Model param low (%f) : ohr low (%f) | param high (%f)
        // 	: ohr high (    %f)\n",x1,h1/totalReqRate,x2,
        // 	h2/totalReqRate);

        if(h2>h1) {
            SHFT3(x0,x1,x2,gss_r*x1+_gss_v*x3);
            SHFT2(h1,h2,modelHitRate(x2));
        } else {
            SHFT3(x3,x2,x1,gss_r*x2+_gss_v*x0);
            SHFT2(h2,h1,modelHitRate(x1));
        }
    }

    // check result
    if( (h1!=h1) || (h2!=h2) ) {
        // numerical failure
        std::cerr << "ERROR: numerical bug " << h1 << " " << h2
                  << std::endl;
        // nop
    } else if (h1 > h2) {
        // x1 should is final parameter
        _cParam = pow(2, x1);
        std::cerr << "Choosing c of " << _cParam << " (log2: " << x1 << ")"
                  << std::endl;
    } else {
        _cParam = pow(2, x2);
        std::cerr << "Choosing c of " << _cParam << " (log2: " << x2 << ")"
                  << std::endl;
    }
}

double AdaptSizeCache::modelHitRate(double log2c) {
    // this code is adapted from the AdaptSize git repo
    // github.com/dasebe/AdaptSize
    double old_T, the_T, the_C;
    double sum_val = 0.;
    double thparam = log2c;

    for(size_t i=0; i<_alignedReqCount.size(); i++) {
        sum_val += _alignedReqCount[i] * (exp(-_alignedObjSize[i]/ pow(2,thparam))) * _alignedObjSize[i];
    }
    if(sum_val <= 0) {
        return(0);
    }
    the_T = getSize() / sum_val;
    // prepare admission probabilities
    _alignedAdmProb.clear();
    for(size_t i=0; i<_alignedReqCount.size(); i++) {
        _alignedAdmProb.push_back(exp(-_alignedObjSize[i]/ pow(2.0,thparam)));
    }
    // 20 iterations to calculate TTL

    for(int j = 0; j<10; j++) {
        the_C = 0;
        if(the_T > 1e70) {
            break;
        }
        for(size_t i=0; i<_alignedReqCount.size(); i++) {
            const double reqTProd = _alignedReqCount[i]*the_T;
            if(reqTProd>150) {
                // cache hit probability = 1, but numerically inaccurate to calculate
                the_C += _alignedObjSize[i];
            } else {
                const double expTerm = exp(reqTProd) - 1;
                const double expAdmProd = _alignedAdmProb[i] * expTerm;
                const double tmp = expAdmProd / (1 + expAdmProd);
                the_C += _alignedObjSize[i] * tmp;
            }
        }
        old_T = the_T;
        the_T = getSize() * old_T/the_C;
    }

    // calculate object hit ratio
    double weighted_hitratio_sum = 0;
    for(size_t i=0; i<_alignedReqCount.size(); i++) {
        const double tmp01= oP1(the_T,_alignedReqCount[i],_alignedAdmProb[i]);
        const double tmp02= oP2(the_T,_alignedReqCount[i],_alignedAdmProb[i]);
        double tmp;
        if(tmp01!=0 && tmp02==0)
            tmp = 0.0;
        else tmp= tmp01/tmp02;
        if(tmp<0.0)
            tmp = 0.0;
        else if (tmp>1.0)
            tmp = 1.0;
        weighted_hitratio_sum += _alignedReqCount[i] * tmp;
    }
    return (weighted_hitratio_sum);
}

/*
  S4LRU
  not optimal: actually segment 0 can hold more than 1/4 if total segments are not full
*/

void S4LRUCache::setSize(const uint64_t &cs) {
    Cache::setSize(cs);
    uint64_t total = cs;
    for (int i = 3; i >= 0; i--) {
        if (i) {
            segments[i].setSize(cs / 4);
            total -= cs / 4;
        } else {
            segments[i].setSize(total);
        }
        std::cerr << "segment " << i << " size : " << segments[i].getSize() << "\n";
    }
}

bool S4LRUCache::lookup(const SimpleRequest &req)
{
    for(int i=0; i<4; i++) {
        if(segments[i].lookup(req)) {
            // hit
            if(i<3) {
                // move up
                segments[i].evict(req.id);
                segment_admit(i+1,req);
            }
            return true;
        }
    }
    return false;
}

void S4LRUCache::admit(const SimpleRequest &req)
{
    segments[0].admit(req);
}

void S4LRUCache::segment_admit(uint8_t idx, const SimpleRequest& req)
{
    if(idx==0) {
        segments[idx].admit(req);
    } else {
        segments[idx].admit(req);
        while(segments[idx].getCurrentSize()
              > segments[idx].getSize()) {
            // need to evict from this partition first
            // find least popular item in this segment
            auto nreq = segments[idx].evict_return();
            segment_admit(idx-1,nreq);
        }
    }
}

void S4LRUCache::evict(SimpleRequest& req)
{
    for(int i=0; i<4; i++) {
        segments[i].evict(req.id);
    }
}

void S4LRUCache::evict()
{
    segments[0].evict();
}

/*
  ThLRU: LRU eviction with a size admission threshold
*/
ThLRUCache::ThLRUCache()
    : LRUCache(),
      _sizeThreshold(524288)
{
}

void ThLRUCache::admit(const SimpleRequest &req)
{
    const uint64_t size = req.size;
    // admit if size < threshold
    if (size < _sizeThreshold) {
        LRUCache::admit(req);
    }
}

void ThS4LRUCache::admit(const SimpleRequest &req)
{
    const uint64_t size = req.size;
    // admit if size < threshold
    if (size < _sizeThreshold) {
        S4LRUCache::admit(req);
    }
}
///*
//  ExpLRU: LRU eviction with size-aware probabilistic cache admission
//*/
//ExpLRUCache::ExpLRUCache()
//    : LRUCache(),
//      _cParam(262144)
//{
//}
//
//void ExpLRUCache::setPar(std::string parName, std::string parValue) {
//    if(parName.compare("c") == 0) {
//        const double c = stof(parValue);
//        assert(c>0);
//        _cParam = pow(2.0,c);
//    } else {
//        std::cerr << "unrecognized parameter: " << parName << std::endl;
//    }
//}
//
//
//
//void ExpLRUCache::admit(const SimpleRequest* req)
//{
//    const double size = req->getSize();
//    // admit to cache with probablity that is exponentially decreasing with size
//    double admissionProb = exp(-size/ _cParam);
//    std::bernoulli_distribution distribution(admissionProb);
//    if (distribution(globalGenerator)) {
//        LRUCache::admit(req);
//    }
//}


