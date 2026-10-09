cmake_minimum_required(VERSION 3.19)
project(mdo_platform_hap LANGUAGES C)
get_filename_component(MDO_PROBE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
get_filename_component(MDO_REPO_ROOT "${MDO_PROBE_ROOT}/../.." ABSOLUTE)
# Read the pinned revision instead of keeping another dependency-version copy.
file(READ "${MDO_REPO_ROOT}/deps.lock" MDO_DEPS_LOCK)
string(JSON MDO_XS_COMMIT GET "${MDO_DEPS_LOCK}" xserver commit)
set(XS_SOURCE_DIR "${MDO_REPO_ROOT}/.build/locked-xs/${MDO_XS_COMMIT}" CACHE PATH "Pinned xs checkout" FORCE)
add_subdirectory("${MDO_PROBE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/probe")
