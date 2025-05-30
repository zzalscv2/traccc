/** TRACCC library, part of the ACTS project (R&D line)
 *
 * (c) 2023-2025 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

#pragma once

// Project include(s).
#include "traccc/utils/prob.hpp"

namespace traccc::device {

TRACCC_HOST_DEVICE inline void build_tracks(
    const global_index_t globalIndex, const build_tracks_payload& payload) {

    const measurement_collection_types::const_device measurements(
        payload.measurements_view);

    const bound_track_parameters_collection_types::const_device seeds(
        payload.seeds_view);

    const vecmem::device_vector<const candidate_link> links(payload.links_view);

    const vecmem::device_vector<const unsigned int> tips(payload.tips_view);

    track_candidate_container_types::device track_candidates(
        payload.track_candidates_view);

    if (globalIndex >= tips.size()) {
        return;
    }

    const auto tip = tips.at(globalIndex);
    auto& seed = track_candidates[globalIndex].header.seed_params;
    auto& trk_quality = track_candidates[globalIndex].header.trk_quality;
    auto cands_per_track = track_candidates[globalIndex].items;

    // Get the link corresponding to tip
    auto L = links.at(tip);
    const unsigned int n_meas = measurements.size();

    const unsigned int n_cands = L.step + 1 - L.n_skipped;

    // Resize the candidates with the exact size
    cands_per_track.resize(n_cands);

    // Track summary variables
    scalar ndf_sum = 0.f;
    scalar chi2_sum = 0.f;

    [[maybe_unused]] std::size_t num_inserted = 0;

    // Reversely iterate to fill the track candidates
    for (auto it = cands_per_track.rbegin(); it != cands_per_track.rend();
         it++) {

        while (L.meas_idx >= n_meas && L.step != 0u) {

            L = links.at(L.previous_candidate_idx);
        }

        assert(L.meas_idx < n_meas);

        *it = {measurements.at(L.meas_idx)};
        num_inserted++;

        // Sanity check on chi2
        assert(L.chi2 < std::numeric_limits<traccc::scalar>::max());
        assert(L.chi2 >= 0.f);

        ndf_sum += static_cast<scalar>(it->meas_dim);
        chi2_sum += L.chi2;

        // Break the loop if the iterator is at the first candidate and fill the
        // seed and track quality
        if (it == cands_per_track.rend() - 1) {
            seed = seeds.at(L.seed_idx);
            trk_quality.ndf = ndf_sum - 5.f;
            trk_quality.chi2 = chi2_sum;
            trk_quality.pval = prob(trk_quality.chi2, trk_quality.ndf);
            trk_quality.n_holes = L.n_skipped;
        } else {
            L = links.at(L.previous_candidate_idx);
        }
    }

#ifndef NDEBUG
    // Assert that we inserted exactly as many elements as we reserved
    // space for.
    assert(num_inserted == cands_per_track.size());

    // Assert that we did not make any duplicate track states.
    for (unsigned int i = 0; i < cands_per_track.size(); ++i) {
        for (unsigned int j = 0; j < cands_per_track.size(); ++j) {
            if (i != j) {
                // TODO: Re-enable me!
                // assert(cands_per_track.at(i).measurement_id !=
                //       cands_per_track.at(j).measurement_id);
            }
        }
    }
#endif
}

}  // namespace traccc::device
