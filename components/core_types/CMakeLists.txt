# core_types — POD definitions shared across the layer boundary.
#
# docs/MODULES.md §2: "Treated as public API — every addition is reviewed,
# because this is the classic shared-domain dumping ground and its erosion
# is how layering quietly dies. If only two modules need a type, it belongs
# to one of them, not here."
#
# Header-only, nothing but stdlib. No REQUIRES: core depends on nothing but
# stdlib + FreeRTOS (docs/ARCHITECTURE.md §1.2), and this doesn't even need
# FreeRTOS.

idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES     ""
    PRIV_REQUIRES ""
)
