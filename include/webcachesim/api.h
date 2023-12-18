//
// Created by zhenyus on 10/31/19.
//

#ifndef WEBCACHESIM_API_H
#define WEBCACHESIM_API_H

#include <stdint.h>
#include <cstdint>
#include <memory>
#include <map>
#include <sys/types.h>

namespace webcachesim {
    const uint max_n_extra_feature = 4;
    const char * const mime_field = "X-extra-fields: ";

    class ParallelCache;

    class Interface {
    public:
        Interface(
                const std::string &cache_type, const uint64_t &cache_size,
                const std::map<std::string, std::string> &params);

        ~Interface() = default;

        void admit(const uint64_t &key, const int64_t &size, const uint16_t extra_features[max_n_extra_feature]);

        uint64_t lookup(const uint64_t &key);

        void set_n_extra_features(const int &_n_extra_features) {
            n_extra_features = _n_extra_features;
        }

        int get_n_extra_features() {
            return n_extra_features;
        }

        int n_extra_features;

    private:
        ParallelCache *pimpl;
    };
}


#endif //WEBCACHESIM_API_H
