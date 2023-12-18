message(${WEBCACHESIM_HEADER_DIR})

set(LIB_SOURCE
        ${WEBCACHESIM_HEADER_DIR}/utils.h
        ${WEBCACHESIM_HEADER_DIR}/file_hash.h
        ${WEBCACHESIM_HEADER_DIR}/cache.h
        ${WEBCACHESIM_HEADER_DIR}/parallel_cache.h
        ${WEBCACHESIM_HEADER_DIR}/simulation.h
        simulation.cpp
        ${WEBCACHESIM_HEADER_DIR}/simulation_tinylfu.h
        simulation_tinylfu.cpp
        ${WEBCACHESIM_HEADER_DIR}/api.h
        api.cpp
        ${WEBCACHESIM_HEADER_DIR}/trace_sanity_check.h
        trace_sanity_check.cpp
        ${WEBCACHESIM_HEADER_DIR}/annotate.h
        caches/annotate.cpp
        ${WEBCACHESIM_HEADER_DIR}/rss.h
        rss.cpp
        ${WEBCACHESIM_HEADER_DIR}/random_helper.h
        random_helper.cpp
        ${WEBCACHESIM_HEADER_DIR}/bloom_filter.h

        ${WEBCACHESIM_HEADER_DIR}/caches/lru_variants.h
        caches/lru_variants.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/belady.h
        caches/belady.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/gd_variants.h
        caches/gd_variants.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/random_variants.h
        caches/random_variants.cpp
        caches/lhd_competitors/lhd_competitors.h
        caches/lhd_competitors/lhd_competitors.cpp
        caches/lhd/lhd_variants.h
        caches/lhd/lhd_variants.cpp
        caches/lhd/lhd.hpp
        caches/lhd/lhd.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/ucb.h
        caches/ucb.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/belady_sample.h
        caches/belady_sample.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/lrb.hpp
        caches/lrb.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/hyperbolic.h
        caches/hyperbolic.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/lecar.h
        caches/lecar.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/lr.h
        caches/lr.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/parallel_lru.h
        caches/parallel_lru.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/parallel_fifo.h
        caches/parallel_fifo.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/parallel_lrb.h
        caches/parallel_lrb.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/binary_relaxed_belady.h
        caches/binary_relaxed_belady.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/percent_relaxed_belady.h
        caches/percent_relaxed_belady.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/relaxed_belady.h
        caches/relaxed_belady.cpp
        ${WEBCACHESIM_HEADER_DIR}/caches/static_parallel_cache.h
        caches/static_parallel_cache.cpp
        )

add_library(webcachesim SHARED ${LIB_SOURCE})
target_include_directories(webcachesim PUBLIC ${WEBCACHESIM_HEADER_DIR})
target_include_directories(webcachesim PUBLIC ${WEBCACHESIM_HEADER_DIR}/caches)
target_include_directories(webcachesim PUBLIC ../lib)

find_package(mongocxx REQUIRED)
find_package(bsoncxx REQUIRED)
target_include_directories(webcachesim PRIVATE ${LIBMONGOCXX_INCLUDE_DIR})
target_include_directories(webcachesim PRIVATE ${LIBBSONCXX_INCLUDE_DIR})
target_link_libraries(webcachesim PRIVATE mongo::bsoncxx_shared)
target_link_libraries(webcachesim PRIVATE mongo::mongocxx_shared)

find_package(Boost REQUIRED)
target_include_directories(webcachesim PRIVATE ${Boost_INCLUDE_DIR})
target_link_libraries(webcachesim LINK_PUBLIC ${Boost_LIBRARIES})

find_path(LIGHTGBM_PATH LightGBM)
if (NOT LIGHTGBM_PATH)
    message(FATAL_ERROR "LIGHTGBM_PATH not found")
endif ()
target_include_directories(webcachesim PUBLIC ${LIGHTGBM_PATH})

find_library(LIGHTGBM_LIB _lightgbm)
if (NOT LIGHTGBM_LIB)
    message(FATAL_ERROR "LIGHTGBM_LIB not found")
endif ()
target_link_libraries(webcachesim LINK_PUBLIC ${LIGHTGBM_LIB})

find_library(BF_LIB bf)
if (NOT BF_LIB)
    message(FATAL_ERROR "BF_LIB not found")
endif ()
target_link_libraries(webcachesim LINK_PUBLIC ${BF_LIB})

find_library(PROCPS_LIB procps)
find_path(PROCPS_PATH proc)
target_include_directories(webcachesim PRIVATE ${PROCPS_PATH})
target_link_libraries(webcachesim PRIVATE ${PROCPS_LIB})

install(TARGETS webcachesim
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
        )

install(DIRECTORY ${WEBCACHESIM_HEADER_DIR} DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
