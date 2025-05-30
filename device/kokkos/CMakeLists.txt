# TRACCC library, part of the ACTS project (R&D line)
#
# (c) 2021-2025 CERN for the benefit of the ACTS project
#
# Mozilla Public License Version 2.0

# Project include(s).
include( traccc-compiler-options-cpp )

traccc_add_library( traccc_kokkos kokkos TYPE SHARED
  # Utility definitions.
  "include/traccc/kokkos/utils/definitions.hpp"
  "include/traccc/kokkos/utils/make_prefix_sum_buff.hpp"
  "src/utils/make_prefix_sum_buff.cpp"
  # Seed finding code.
  "include/traccc/kokkos/seeding/details/spacepoint_binning.hpp"
  "src/seeding/spacepoint_binning.cpp"
)

thrust_create_target (traccc::kokkos_thrust
  HOST CPP
  DEVICE CPP )

target_link_libraries( traccc_kokkos
  PUBLIC traccc::core vecmem::core Kokkos::kokkos traccc::kokkos_thrust
  PRIVATE traccc::device_common )
