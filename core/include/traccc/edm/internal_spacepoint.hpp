/** TRACCC library, part of the ACTS project (R&D line)
 *
 * (c) 2021-2022 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

#pragma once

// traccc include
#include "traccc/definitions/primitives.hpp"
#include "traccc/edm/container.hpp"

// detray core
#include <detray/utils/invalid_values.hpp>

// Algebra plugins include(s).
#include <algebra/math/common.hpp>

namespace traccc {

/// Item: A internal spacepoint definition
template <typename spacepoint_t>
struct internal_spacepoint {

    using link_type = typename collection_types<spacepoint_t>::host::size_type;
    link_type m_link;

    scalar m_x;
    scalar m_y;
    scalar m_z;
    scalar m_r;
    scalar m_phi;

    internal_spacepoint() = default;

    TRACCC_HOST_DEVICE internal_spacepoint(const spacepoint_t& sp,
                                           const link_type sp_link,
                                           const vector2& offsetXY)
        : m_link(sp_link) {
        m_x = sp.global[0] - offsetXY[0];
        m_y = sp.global[1] - offsetXY[1];
        m_z = sp.global[2];
        m_r = algebra::math::sqrt(m_x * m_x + m_y * m_y);
        m_phi = algebra::math::atan2(m_y, m_x);
    }

    TRACCC_HOST_DEVICE
    internal_spacepoint(const link_type& sp_link) : m_link(sp_link) {

        m_x = 0;
        m_y = 0;
        m_z = 0;
        m_r = 0;
        m_phi = 0;
    }

    TRACCC_HOST_DEVICE
    static inline internal_spacepoint<spacepoint_t> invalid_value() {

        link_type l = detray::detail::invalid_value<link_type>();

        return internal_spacepoint<spacepoint_t>({std::move(l)});
    }

    TRACCC_HOST_DEVICE const scalar& x() const { return m_x; }

    TRACCC_HOST_DEVICE
    scalar y() const { return m_y; }

    TRACCC_HOST_DEVICE
    scalar z() const { return m_z; }

    TRACCC_HOST_DEVICE
    scalar radius() const { return m_r; }

    TRACCC_HOST_DEVICE
    scalar phi() const { return m_phi; }

    TRACCC_HOST_DEVICE
    scalar varianceR() const { return 0.f; }

    TRACCC_HOST_DEVICE
    scalar varianceZ() const { return 0.f; }
};

template <typename spacepoint_t>
inline bool operator<(const internal_spacepoint<spacepoint_t>& lhs,
                      const internal_spacepoint<spacepoint_t>& rhs) {
    return (lhs.radius() < rhs.radius());
}

}  // namespace traccc
