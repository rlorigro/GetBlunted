######################################
# ---------------------------------- #
# -------- COMPILER VERSION -------- #
# ---------------------------------- #
######################################

cmake_minimum_required(VERSION 3.0)
project(GetBlunted VERSION 0.0.0)

message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "CMAKE_SYSTEM: ${CMAKE_SYSTEM}")

# Compiler options.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------- Memory debug ------- #

#set(CMAKE_CXX_FLAGS "-fexceptions -fsanitize=address")
#set(ASAN_OPTIONS=check_initialization_order=1)
#set(ASAN_OPTIONS=detect_leaks=1)

# --- Standard compilation --- #

set(CMAKE_CXX_FLAGS "-fexceptions")

# ---------------------------- #

#add_definitions(-ggdb3 -O0 -Wall)       # Debugging + No optimization
add_definitions(-O3 -Wall)              # Much optimization

#########################################
# ------------------------------------- #
# -------- SOURCES AND HEADERS -------- #
# ------------------------------------- #
#########################################

# Include header files
include_directories(
        "inc"
)

# Define our shared library sources. NOT test/executables.
set(SOURCES
        src/AdjacencyComponent.cpp
        src/apply_bulk_modifications.cpp
	    src/BicliqueCover.cpp
	    src/Biclique.cpp
	    src/BipartiteGraph.cpp
        src/Bluntifier.cpp
	    src/copy_graph.cpp
        src/Cigar.cpp
        src/Duplicator.cpp
        src/duplicate_terminus.cpp
        src/find_tips.cpp
	    src/GaloisLattice.cpp
        src/gfa_to_handle.cpp
        src/handle_to_gfa.cpp
        src/IncrementalIdMap.cpp
        src/is_single_stranded.cpp
        src/NodeInfo.cpp
        src/OverlapMap.cpp
        src/OverlappingOverlap.cpp
        src/OverlappingOverlapSplicer.cpp
        src/BluntifierAlign.cpp
	    src/ReducedDualGraph.cpp
        src/SubtractiveHandleGraph.cpp
        src/Subgraph.cpp
        src/topological_sort.cpp
        src/traverse.cpp
        src/unchop.cpp
        src/utility.cpp
        src/VertexColoring.cpp
        )


project(GetBlunted)
add_library(GetBlunted STATIC ${SOURCES}) 

# To make sure the library is named Name.so,
# get rid of the "lib" prefix.
set_target_properties(GetBlunted PROPERTIES PREFIX "")

# Eliminate an extraneous -D during compilation.
set_target_properties(GetBlunted PROPERTIES DEFINE_SYMBOL "")


##############################################
# ------------------------------------------ #
# --------- LINKING NATIVE LIBRARY --------- #
# ------------------ OMP ------------------- #
# ------------------------------------------ #
##############################################


if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

    # assumes clang build
    # we can't reliably detect when we're using clang, so for the time being we assume
    # TODO: can't we though?

    # adapted from https://stackoverflow.com/questions/46414660/macos-cmake-and-openmp
    # find_package(OpenMP) does not work reliably on macOS, so we do its work ourselves
    if(EXISTS /usr/local/Cellar/libomp/)
        set(OMPDIR /usr/local/Cellar/libomp/)
    elseif(EXISTS /opt/local/include/libomp)
        set(OMPDIR /opt/local/include/libomp)
    endif()

    set (OpenMP_C "${CMAKE_C_COMPILER}")
    set (OpenMP_C_FLAGS " -Xpreprocessor -fopenmp -I${OMPDIR} -I/usr/local/include -L${OMPDIR} -L/usr/local/lib")
    set (OpenMP_C_LIB_NAMES "libomp" "libgomp" "libiomp5")
    set (OpenMP_CXX "${CMAKE_CXX_COMPILER}")
    set (OpenMP_CXX_FLAGS " -Xpreprocessor -fopenmp -I${OMPDIR} -I/usr/local/include -L${OMPDIR} -L/usr/local/lib")
    set (OpenMP_CXX_LIB_NAMES "libomp" "libgomp" "libiomp5")
    set (OpenMP_libomp_LIBRARY "omp")
    set (OpenMP_libgomp_LIBRARY "gomp")
    set (OpenMP_libiomp5_LIBRARY "iomp5")

    # and now add the OpenMP parameters to the compile flags
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS} -lomp")

    # Mac needs libdl and libomp when linking the library
    set(PLATFORM_EXTRA_LIB_FLAGS -ldl -lomp)


elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")

    find_package(OpenMP REQUIRED)

endif()


##############################################
# ------------------------------------------ #
# -------- LINKING EXTERNAL LIBRARY -------- #
# ---------------- gfakluge ---------------- #
# ------------------------------------------ #
##############################################

# Need to explicitly enable ExternalProject functionality
include(ExternalProject)

# Download or update library as an external project
ExternalProject_Add(project_gfak
        GIT_REPOSITORY https://github.com/edawson/gfakluge.git
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}/external/
        CONFIGURE_COMMAND ""
#	    DOWNLOAD_COMMAND ""
#        UPDATE_COMMAND ""
        BUILD_IN_SOURCE True
        INSTALL_DIR ${CMAKE_SOURCE_DIR}/external/gfak/
        INSTALL_COMMAND make PREFIX=${CMAKE_SOURCE_DIR}/external/gfak/ install
        )

# Define INSTALL_DIR as the install directory for external library
ExternalProject_Get_Property(project_gfak INSTALL_DIR)


# THIS SECTION QUARANTINED FOR MAC compatibility :'(
# ---
# Create new library for external project (so it can be linked with main library)
#add_library(gfak STATIC IMPORTED)
#set_property(TARGET gfak
#        PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/bin/gfak)
#
## Define library as dependent on the downloaded project
#add_dependencies(gfak project_gfak)
# ---


# Define main library as dependent on the downloaded project (transitively)
add_dependencies(GetBlunted project_gfak)

# Ensure that main library has access to primary dependencies' and secondary dependencies' headers
include_directories(external/gfak/include/)

# Add runtime path for main library so it can access external library
#set_property(TARGET GetBlunted PROPERTY INSTALL_RPATH "$ORIGIN" "${INSTALL_DIR}/bin/gfak")

message(STATUS "INSTALL_DIR: ${INSTALL_DIR}")


##############################################
# ------------------------------------------ #
# -------- LINKING EXTERNAL LIBRARY -------- #
# ------------------ bdsg ------------------ #
# ------------------------------------------ #
##############################################

# Need to explicitly enable ExternalProject functionality
include(ExternalProject)

# Download or update library as an external project
ExternalProject_Add(project_bdsg
        GIT_REPOSITORY https://github.com/vgteam/libbdsg-easy.git
        GIT_TAG b989949811918b18de48a5e57d7f45473d8901f1
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}/external/
        CONFIGURE_COMMAND ""
#        DOWNLOAD_COMMAND ""
#        UPDATE_COMMAND ""
        BUILD_IN_SOURCE True
        INSTALL_DIR ${CMAKE_SOURCE_DIR}/external/bdsg/
        INSTALL_COMMAND make INSTALL_PREFIX=${CMAKE_SOURCE_DIR}/external/bdsg/ install
        )

# Define INSTALL_DIR as the install directory for external library
ExternalProject_Get_Property(project_bdsg INSTALL_DIR)

# Create new library for external project (so it can be linked with main library)
add_library(bdsg STATIC IMPORTED)
set_property(TARGET bdsg
        PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libbdsg.a)

add_library(divsufsort STATIC IMPORTED)
set_property(TARGET divsufsort
        PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libdivsufsort64.a)

add_library(libhandlegraph STATIC IMPORTED)
set_property(TARGET libhandlegraph
        PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libhandlegraph.a)

add_library(libsdsl STATIC IMPORTED)
set_property(TARGET libsdsl
        PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libsdsl.a)

# Define library as dependent on the downloaded project
add_dependencies(bdsg
        project_bdsg
        libsdsl
        libhandlegraph
        divsufsort)

# Define main library as dependent on the downloaded project (transitively)
add_dependencies(GetBlunted bdsg)

# Ensure that main library has access to primary dependencies' and secondary dependencies' headers
include_directories(external/bdsg/include/
        external/bdsg/include/handlegraph/
        external/bdsg/include/bdsg/)

# Add runtime path for main library so it can access external library
#set_property(TARGET GetBlunted PROPERTY INSTALL_RPATH "$ORIGIN" "${INSTALL_DIR}/bin/bdsg")

message(STATUS "INSTALL_DIR: ${INSTALL_DIR}")


##############################################
# ------------------------------------------ #
# -------- LINKING EXTERNAL LIBRARY -------- #
# ------------------ spoa ------------------ #
# ------------------------------------------ #
##############################################

# Need to explicitly enable ExternalProject functionality
include(ExternalProject)

if(${CMAKE_VERSION} VERSION_LESS "3.19.0")
    # Download or update library as an external project
    ExternalProject_Add(project_spoa
            GIT_REPOSITORY https://github.com/rvaser/spoa.git
            GIT_SUBMODULES vendor/bioparser vendor/cereal vendor/cpu_features vendor/simde
            #        DOWNLOAD_COMMAND ""
            #        UPDATE_COMMAND ""
            PREFIX ${CMAKE_CURRENT_BINARY_DIR}/external/
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_SOURCE_DIR}/external/spoa/ -DCMAKE_INSTALL_LIBDIR=${CMAKE_SOURCE_DIR}/external/spoa/lib
            BUILD_IN_SOURCE True
            INSTALL_DIR ${CMAKE_SOURCE_DIR}/external/spoa/
            INSTALL_COMMAND make install
            )
else()
    # Download or update library as an external project
    ExternalProject_Add(project_spoa
            GIT_REPOSITORY https://github.com/rvaser/spoa.git
            #        DOWNLOAD_COMMAND ""
            #        UPDATE_COMMAND ""
            PREFIX ${CMAKE_CURRENT_BINARY_DIR}/external/
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_SOURCE_DIR}/external/spoa/ -DCMAKE_INSTALL_LIBDIR=${CMAKE_SOURCE_DIR}/external/spoa/lib
            BUILD_IN_SOURCE True
            INSTALL_DIR ${CMAKE_SOURCE_DIR}/external/spoa/
            INSTALL_COMMAND make install
            )
endif()

# Define INSTALL_DIR as the install directory for external library
ExternalProject_Get_Property(project_spoa INSTALL_DIR)

# Create new library for external project (so it can be linked with main library)
add_library(spoa STATIC IMPORTED)
set_property(TARGET spoa
        PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libspoa.a)

# Define library as dependent on the downloaded project
add_dependencies(spoa
        project_spoa
        )

# Define main library as dependent on the downloaded project (transitively)
add_dependencies(GetBlunted spoa)

# Ensure that main library has access to primary dependencies' and secondary dependencies' headers
include_directories(external/spoa/include/
        ${CMAKE_CURRENT_BINARY_DIR}/external/src/project_spoa/vendor/cereal/include/
        ${CMAKE_CURRENT_BINARY_DIR}/external/src/project_spoa/vendor/cpu_features/include/
        ${CMAKE_CURRENT_BINARY_DIR}/external/src/project_spoa/vendor/simde/simde/
        ${CMAKE_CURRENT_BINARY_DIR}/external/src/project_spoa/vendor/simde/arm/
        ${CMAKE_CURRENT_BINARY_DIR}/external/src/project_spoa/vendor/simde/arm/neon/
        )

# Add runtime path for main library so it can access external library
#set_property(TARGET GetBlunted PROPERTY INSTALL_RPATH "$ORIGIN" "${INSTALL_DIR}/bin/spoa")

message(STATUS "INSTALL_DIR: ${INSTALL_DIR}")

############################################
# ---------------------------------------- #
# -------- Generating executables -------- #
# ---------------------------------------- #
############################################

# --- Recipe for run time linking taken from CMake docs ---#
# https://gitlab.kitware.com/cmake/community/-/wikis/doc/cmake/RPATH-handling#always-full-rpath
# use, i.e. don't skip the full RPATH for the build tree
set(CMAKE_SKIP_BUILD_RPATH FALSE)

# when building, don't use the install RPATH already
# (but later on when installing)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# the RPATH to be used when installing, but only if it's not a system directory
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
if("${isSystemDir}" STREQUAL "-1")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
endif("${isSystemDir}" STREQUAL "-1")

# ---------------------------------------------------------#


set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


# -------- TESTS --------

set(TESTS
        test_AdjacencyComponent
        test_bdsg
	    test_BicliqueCover
        test_cigar
        test_divide_handle
        test_duplicate_terminus
        test_gfak
        test_handlegraph
        test_handle_to_gfa
        test_IncrementalIdMap
        test_map_range_methods
        test_overlaps
        test_spoa
        test_utility
        )

foreach(FILENAME_PREFIX ${TESTS})
    add_executable(${FILENAME_PREFIX} src/test/${FILENAME_PREFIX}.cpp)
    target_link_libraries(${FILENAME_PREFIX}
            GetBlunted
            Threads::Threads
            bdsg
            spoa
            divsufsort
            libhandlegraph
            libsdsl)

    if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        target_link_libraries(${FILENAME_PREFIX}
                OpenMP::OpenMP_CXX)
    endif()
endforeach()


# -------- EXECUTABLES --------

set(EXECUTABLES
        get_blunted
        )

foreach(FILENAME_PREFIX ${EXECUTABLES})
    add_executable(${FILENAME_PREFIX} src/executable/${FILENAME_PREFIX}.cpp)
    target_link_libraries(${FILENAME_PREFIX}
            GetBlunted
            Threads::Threads
            bdsg
            spoa
            divsufsort
            libhandlegraph
            libsdsl)

    if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        target_link_libraries(${FILENAME_PREFIX}
                OpenMP::OpenMP_CXX)
    endif()
endforeach()

# -------- final steps --------

# Where to install
set(BINARY_INSTALL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# The shared library goes to the bin directory.
install(TARGETS
        GetBlunted
        get_blunted
        DESTINATION ${BINARY_INSTALL_DIR})

# Some potentially helpful messages
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message(STATUS "CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
message(STATUS "CMAKE_CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "CMAKE_CURRENT_SOURCE_DIR: ${CMAKE_CURRENT_SOURCE_DIR}")

