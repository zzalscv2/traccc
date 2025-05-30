/** TRACCC library, part of the ACTS project (R&D line)
 *
 * (c) 2023-2025 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

#pragma once

// HACK: Fix for intel/llvm#15544
// As of Intel LLVM 2025.0, enabling an AMD SYCL target inadvertently sets the
// `__CUDA_ARCH__` preprocessor definition which breaks all sorts of internal
// logic in Thrust. Thus, we very selectively undefine the `__CUDA_ARCH__`
// definition when we are are compiling SYCL code using the Intel LLVM
// compiler. This can be removed when intel/llvm#15443 makes it into a OneAPI
// release.
#include <limits>
#if defined(__INTEL_LLVM_COMPILER) && defined(SYCL_LANGUAGE_VERSION)
#undef __CUDA_ARCH__
#endif

// Project include(s).
#include "traccc/fitting/kalman_filter/gain_matrix_updater.hpp"
#include "traccc/fitting/status_codes.hpp"

// Detray include(s)
#include <detray/geometry/tracking_surface.hpp>

// Thrust include(s).
#include <thrust/binary_search.h>
#include <thrust/execution_policy.h>

namespace traccc::device {

template <typename detector_t, concepts::thread_id1 thread_id_t,
          concepts::barrier barrier_t>
TRACCC_HOST_DEVICE inline void find_tracks(
    const thread_id_t& thread_id, const barrier_t& barrier,
    const finding_config& cfg, const find_tracks_payload<detector_t>& payload,
    const find_tracks_shared_payload& shared_payload) {

    /*
     * Initialize the block-shared data; in particular, set the total size of
     * the candidate buffer to zero, and then set the number of candidates for
     * each parameter to zero.
     */
    if (thread_id.getLocalThreadIdX() == 0) {
        shared_payload.shared_candidates_size = 0;
    }

    shared_payload.shared_num_candidates[thread_id.getLocalThreadIdX()] = 0;

    barrier.blockBarrier();

    /*
     * Initialize all of the device vectors from their vecmem views.
     */
    detector_t det(payload.det_data);
    measurement_collection_types::const_device measurements(
        payload.measurements_view);
    bound_track_parameters_collection_types::const_device in_params(
        payload.in_params_view);
    vecmem::device_vector<const unsigned int> in_params_liveness(
        payload.in_params_liveness_view);
    vecmem::device_vector<candidate_link> links(payload.links_view);
    bound_track_parameters_collection_types::device out_params(
        payload.out_params_view);
    vecmem::device_vector<unsigned int> out_params_liveness(
        payload.out_params_liveness_view);
    vecmem::device_vector<const detray::geometry::barcode> barcodes(
        payload.barcodes_view);
    vecmem::device_vector<const unsigned int> upper_bounds(
        payload.upper_bounds_view);
    vecmem::device_vector<unsigned int> tips(payload.tips_view);
    vecmem::device_vector<unsigned int> n_tracks_per_seed(
        payload.n_tracks_per_seed_view);

    const unsigned int in_param_id = thread_id.getGlobalThreadIdX();

    const bool last_step =
        payload.step == cfg.max_track_candidates_per_track - 1;

    /*
     * Step 1 of this kernel is to determine which indices belong to which
     * parameter. Because the measurements are guaranteed to be grouped, we can
     * simply find the first measurement's index and the total number of
     * indices.
     *
     * This entire step is executed on a one-thread-one-parameter model.
     */
    unsigned int init_meas = 0;
    unsigned int num_meas = 0;

    if (in_param_id < payload.n_in_params &&
        in_params_liveness.at(in_param_id) > 0u) {

        /*
         * Get the barcode of this thread's parameters, then find the first
         * measurement that matches it.
         */
        const auto bcd = in_params.at(in_param_id).surface_link();
        const auto lo = thrust::lower_bound(thrust::seq, barcodes.begin(),
                                            barcodes.end(), bcd);

        /*
         * If we cannot find any corresponding measurements, set the number of
         * measurements to zero.
         */
        if (lo == barcodes.end()) {
            init_meas = 0;
        }
        /*
         * If measurements are found, use the previously (outside this kernel)
         * computed upper bound array to compute the range of measurements for
         * this thread.
         */
        else {
            const vecmem::device_vector<const unsigned int>::size_type bcd_id =
                static_cast<
                    vecmem::device_vector<const unsigned int>::size_type>(
                    std::distance(barcodes.begin(), lo));

            init_meas = lo == barcodes.begin() ? 0u : upper_bounds[bcd_id - 1];
            num_meas = upper_bounds[bcd_id] - init_meas;
        }
    }

    /*
     * Step 2 of this kernel involves processing the candidate measurements and
     * updating them on their corresponding surface.
     *
     * Because the number of measurements per parameter can vary wildly
     * (between 0 and 20), a naive one-thread-one-parameter model would incur a
     * lot of thread divergence here. Instead, we use a load-balanced model in
     * which threads process each others' measurements.
     *
     * The core idea is that each thread places its measurements into a shared
     * pool. We keep track of how many measurements each thread has placed into
     * the pool.
     */
    unsigned int curr_meas = 0;

    /*
     * This loop keeps running until all threads have processed all of their
     * measurements.
     */
    while (barrier.blockOr(curr_meas < num_meas ||
                           shared_payload.shared_candidates_size > 0)) {
        /*
         * The outer loop consists of three general components. The first
         * components is that each thread starts to fill a shared buffer of
         * measurements. The buffer is twice the size of the block to
         * accomodate any overflow.
         *
         * Threads insert their measurements into the shared buffer until they
         * either run out of measurements, or until the shared buffer is full.
         */
        for (; curr_meas < num_meas &&
               shared_payload.shared_candidates_size < thread_id.getBlockDimX();
             curr_meas++) {
            const unsigned int prev_link_idx =
                payload.prev_links_idx + thread_id.getGlobalThreadIdX();
            const unsigned int seed_idx = payload.step > 0
                                              ? links.at(prev_link_idx).seed_idx
                                              : in_param_id;
            if (n_tracks_per_seed.at(seed_idx) >=
                cfg.max_num_branches_per_seed) {
                // We will not use this parameter anymore
                curr_meas = num_meas;
                break;
            }
            unsigned int idx =
                vecmem::device_atomic_ref<unsigned int,
                                          vecmem::device_address_space::local>(
                    shared_payload.shared_candidates_size)
                    .fetch_add(1u);

            /*
             * The buffer elemements are tuples of the measurement index and
             * the index of the thread that originally inserted that
             * measurement.
             */
            shared_payload.shared_candidates[idx] = {
                init_meas + curr_meas, thread_id.getLocalThreadIdX()};
        }

        barrier.blockBarrier();

        /*
         * The shared buffer is now full; each thread picks out zero or one of
         * the measurements and processes it.
         */
        if (thread_id.getLocalThreadIdX() <
            shared_payload.shared_candidates_size) {
            const unsigned int owner_local_thread_id =
                shared_payload.shared_candidates[thread_id.getLocalThreadIdX()]
                    .second;
            const unsigned int owner_global_thread_id =
                owner_local_thread_id +
                thread_id.getBlockDimX() * thread_id.getBlockIdX();
            assert(in_params_liveness.at(owner_global_thread_id) != 0u);
            const unsigned int prev_link_idx =
                payload.prev_links_idx + owner_global_thread_id;
            const unsigned int seed_idx =
                payload.step == 0 ? owner_global_thread_id
                                  : links.at(prev_link_idx).seed_idx;
            const bound_track_parameters<>& in_par =
                in_params.at(owner_global_thread_id);
            const unsigned int meas_idx =
                shared_payload.shared_candidates[thread_id.getLocalThreadIdX()]
                    .first;

            const auto& meas = measurements.at(meas_idx);

            track_state<typename detector_t::algebra_type> trk_state(meas);
            const detray::tracking_surface sf{det, in_par.surface_link()};

            // Count the number of tracks per seed
            vecmem::device_atomic_ref<unsigned int> num_tracks_per_seed(
                n_tracks_per_seed.at(seed_idx));

            // Count the number of branches per input parameter
            vecmem::device_atomic_ref<unsigned int,
                                      vecmem::device_address_space::local>
                shared_num_candidates(
                    shared_payload
                        .shared_num_candidates[owner_local_thread_id]);

            bool add_link =
                num_tracks_per_seed.load() < cfg.max_num_branches_per_seed;
            if (add_link) {
                // Run the Kalman update
                const kalman_fitter_status res = sf.template visit_mask<
                    gain_matrix_updater<typename detector_t::algebra_type>>(
                    trk_state, in_par);
                // The chi2 from Kalman update should be less than chi2_max
                add_link = res == kalman_fitter_status::SUCCESS &&
                           trk_state.filtered_chi2() < cfg.chi2_max;
            } else {
                // The seed is already exhausted, avoid adding hole
                shared_num_candidates.fetch_add(1u);
            }
            if (add_link) {
                // Increase the number of candidates (or branches) per input
                // parameter. Avoid adding hole when seed is saturated
                shared_num_candidates.fetch_add(1u);

                const unsigned int s_pos = num_tracks_per_seed.fetch_add(1);
                add_link = s_pos < cfg.max_num_branches_per_seed;
            }

            if (add_link) {
                // Add measurement candidates to link
                const unsigned int l_pos = links.bulk_append_implicit(1);

                const traccc::scalar chi2 = trk_state.filtered_chi2();
                assert(chi2 >= 0.f);

                const unsigned int n_skipped =
                    payload.step == 0 ? 0 : links.at(prev_link_idx).n_skipped;

                links.at(l_pos) = {.step = payload.step,
                                   .previous_candidate_idx = prev_link_idx,
                                   .meas_idx = meas_idx,
                                   .seed_idx = seed_idx,
                                   .n_skipped = n_skipped,
                                   .chi2 = chi2};
                out_params.at(l_pos - payload.curr_links_idx) =
                    trk_state.filtered();
                out_params_liveness.at(l_pos - payload.curr_links_idx) =
                    static_cast<unsigned int>(!last_step);

                const unsigned int n_cands = payload.step + 1 - n_skipped;

                // If no more CKF step is expected, current candidate is kept as
                // a tip
                if (last_step &&
                    n_cands >= cfg.min_track_candidates_per_track) {
                    tips.push_back(l_pos);
                }
            }
        }

        barrier.blockBarrier();

        /*
         * The reason the buffer is twice the size of the block is that we
         * might end up having some spill-over; this spill-over should be moved
         * to the front of the buffer.
         */
        shared_payload.shared_candidates[thread_id.getLocalThreadIdX()] =
            shared_payload.shared_candidates[thread_id.getLocalThreadIdX() +
                                             thread_id.getBlockDimX()];

        if (thread_id.getLocalThreadIdX() == 0) {
            if (shared_payload.shared_candidates_size >=
                thread_id.getBlockDimX()) {
                shared_payload.shared_candidates_size -=
                    thread_id.getBlockDimX();
            } else {
                shared_payload.shared_candidates_size = 0;
            }
        }
    }

    /*
     * Part three of the kernel inserts holes for parameters which did not
     * match any measurements.
     */
    if (in_param_id < payload.n_in_params &&
        in_params_liveness.at(in_param_id) != 0u &&
        shared_payload.shared_num_candidates[thread_id.getLocalThreadIdX()] ==
            0u) {

        const unsigned int prev_link_idx = payload.prev_links_idx + in_param_id;
        const unsigned int seed_idx =
            payload.step == 0 ? in_param_id : links.at(prev_link_idx).seed_idx;

        vecmem::device_atomic_ref<unsigned int> num_tracks_per_seed(
            n_tracks_per_seed.at(seed_idx));
        const unsigned int s_pos = num_tracks_per_seed.fetch_add(1);

        if (s_pos < cfg.max_num_branches_per_seed) {
            const unsigned int n_skipped =
                payload.step == 0 ? 0 : links.at(prev_link_idx).n_skipped;

            if (n_skipped >= cfg.max_num_skipping_per_cand || last_step) {
                const unsigned int n_cands = payload.step - n_skipped;
                if (n_cands >= cfg.min_track_candidates_per_track) {
                    // In case of max skipping and min length being 0, and first
                    // step being skipped, the links are empty, and the tip has
                    // nowhere to point
                    assert(payload.step > 0);
                    tips.push_back(prev_link_idx);
                }
            } else {
                // Add measurement candidates to link
                const unsigned int l_pos = links.bulk_append_implicit(1);

                links.at(l_pos) = {
                    .step = payload.step,
                    .previous_candidate_idx = prev_link_idx,
                    .meas_idx = std::numeric_limits<unsigned int>::max(),
                    .seed_idx = seed_idx,
                    .n_skipped = n_skipped + 1,
                    .chi2 = std::numeric_limits<traccc::scalar>::max()};

                out_params.at(l_pos - payload.curr_links_idx) =
                    in_params.at(in_param_id);
                out_params_liveness.at(l_pos - payload.curr_links_idx) = 1u;
            }
        }
    }
}
}  // namespace traccc::device
