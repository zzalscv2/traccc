/** TRACCC library, part of the ACTS project (R&D line)
 *
 * (c) 2023-2025 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

// Project include(s).
#include "tests/cca_test.hpp"
#include "traccc/definitions/common.hpp"
#include "traccc/geometry/silicon_detector_description.hpp"
#include "traccc/performance/collection_comparator.hpp"
#include "traccc/sycl/clusterization/clusterization_algorithm.hpp"

// VecMem include(s).
#include <vecmem/memory/sycl/shared_memory_resource.hpp>
#include <vecmem/utils/sycl/copy.hpp>

// GTest include(s).
#include <gtest/gtest.h>

using namespace traccc;

TEST(SYCLClustering, SingleModule) {

    // Creating SYCL queue object
    vecmem::sycl::queue_wrapper vecmem_queue;
    traccc::sycl::queue_wrapper traccc_queue{vecmem_queue.queue()};
    std::cout << "Running on device: " << vecmem_queue.device_name() << "\n";

    // Memory resource used by the EDM.
    vecmem::sycl::shared_memory_resource shared_mr{vecmem_queue};
    traccc::memory_resource mr{shared_mr};

    // Copy object
    vecmem::sycl::copy copy{vecmem_queue};

    // Create cell collection
    traccc::edm::silicon_cell_collection::host cells{shared_mr};
    cells.resize(8u);
    cells.channel0() = {1u, 2u, 3u, 6u, 5u, 6u, 7u, 6u};
    cells.channel1() = {2u, 2u, 2u, 4u, 5u, 5u, 5u, 6u};
    cells.activation() = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    cells.time() = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    cells.module_index() = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

    // Create a dummy detector description.
    traccc::silicon_detector_description::host dd{shared_mr};
    dd.resize(1u);
    dd.reference_x()[0] = 0.f;
    dd.reference_y()[0] = 0.f;
    dd.pitch_x()[0] = 1.f;
    dd.pitch_y()[0] = 1.f;
    dd.dimensions()[0] = 2;
    dd.geometry_id()[0] = detray::geometry::barcode{0u};

    // Run Clusterization
    traccc::sycl::clusterization_algorithm ca_sycl(mr, copy, traccc_queue,
                                                   default_ccl_test_config());

    auto measurements_buffer =
        ca_sycl(vecmem::get_data(cells), vecmem::get_data(dd));

    measurement_collection_types::device measurements(measurements_buffer);

    // Check the results
    EXPECT_EQ(copy.get_size(measurements_buffer), 2u);

    measurement_collection_types::host references;
    references.push_back(
        {{2.5f, 2.5f}, {0.75f, 0.0833333f}, detray::geometry::barcode{0u}});
    references.push_back(
        {{6.5f, 5.5f}, {0.483333f, 0.483333f}, detray::geometry::barcode{0u}});

    for (const auto& test : measurements) {
        // 0.01 % uncertainty
        auto iso = traccc::details::is_same_object<measurement>(test, 0.0001f);
        bool matched = false;

        for (const auto& ref : references) {
            if (iso(ref)) {
                matched = true;
                break;
            }
        }

        ASSERT_TRUE(matched);
    }
}
