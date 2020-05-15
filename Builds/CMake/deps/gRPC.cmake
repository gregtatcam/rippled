
# currently linking to unsecure versions...if we switch, we'll
# need to add ssl as a link dependency to the grpc targets
option (use_secure_grpc "use TLS version of grpc libs." OFF)
if (use_secure_grpc)
  set (grpc_suffix "")
else ()
  set (grpc_suffix "_unsecure")
endif ()

set(grpc_cpp_plugin_ $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
set(grpc++_ "gRPC::grpc++${grpc_suffix}")

find_package (gRPC 1.23 CONFIG QUIET)
if (TARGET gRPC::gpr AND NOT local_grpc)
  get_target_property (_grpc_l gRPC::gpr IMPORTED_LOCATION_DEBUG)
  if (NOT _grpc_l)
    get_target_property (_grpc_l gRPC::gpr IMPORTED_LOCATION_RELEASE)
  endif ()
  if (NOT _grpc_l)
    get_target_property (_grpc_l gRPC::gpr IMPORTED_LOCATION)
  endif ()
  message (STATUS "Found cmake config for gRPC. Using ${_grpc_l}.")
else ()
  find_package (PkgConfig QUIET)
  if (PKG_CONFIG_FOUND)
      pkg_check_modules (grpc QUIET "grpc${grpc_suffix}>=1.25" "grpc++${grpc_suffix}" gpr)
  endif ()

  if (grpc_FOUND)
    message (STATUS "Found gRPC using pkg-config. Using ${grpc_gpr_PREFIX}.")
  endif ()

  add_executable (gRPC::grpc_cpp_plugin IMPORTED)
  exclude_if_included (gRPC::grpc_cpp_plugin)

  if (grpc_FOUND AND NOT local_grpc)
    # use installed grpc (via pkg-config)
    macro (add_imported_grpc libname_)
      if (static)
        set (_search "${CMAKE_STATIC_LIBRARY_PREFIX}${libname_}${CMAKE_STATIC_LIBRARY_SUFFIX}")
      else ()
        set (_search "${CMAKE_SHARED_LIBRARY_PREFIX}${libname_}${CMAKE_SHARED_LIBRARY_SUFFIX}")
      endif()
      find_library(_found_${libname_}
        NAMES ${_search}
        HINTS ${grpc_LIBRARY_DIRS})
      if (_found_${libname_})
        message (STATUS "importing ${libname_} as ${_found_${libname_}}")
      else ()
        message (FATAL_ERROR "using pkg-config for grpc, can't find ${_search}")
      endif ()
      add_library ("gRPC::${libname_}" STATIC IMPORTED GLOBAL)
      set_target_properties ("gRPC::${libname_}" PROPERTIES IMPORTED_LOCATION ${_found_${libname_}})
      if (grpc_INCLUDE_DIRS)
        set_target_properties ("gRPC::${libname_}" PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${grpc_INCLUDE_DIRS})
      endif ()
      target_link_libraries (ripple_libs INTERFACE "gRPC::${libname_}")
      exclude_if_included ("gRPC::${libname_}")
    endmacro ()
    add_imported_grpc("grpc++${grpc_suffix}")

    set_target_properties (gRPC::grpc_cpp_plugin PROPERTIES
        IMPORTED_LOCATION "${grpc_gpr_PREFIX}/bin/grpc_cpp_plugin${CMAKE_EXECUTABLE_SUFFIX}")

    pkg_check_modules (cares QUIET libcares)
    if (cares_FOUND)
      if (static)
        set (_search "${CMAKE_STATIC_LIBRARY_PREFIX}cares${CMAKE_STATIC_LIBRARY_SUFFIX}")
      else ()
        set (_search "${CMAKE_SHARED_LIBRARY_PREFIX}cares${CMAKE_SHARED_LIBRARY_SUFFIX}")
      endif()
      find_library(_cares
        NAMES ${_search}
        HINTS ${cares_LIBRARY_DIRS})
      if (NOT _cares)
        message (FATAL_ERROR "using pkg-config for grpc, can't find c-ares")
      endif ()
      add_library (c-ares::cares STATIC IMPORTED GLOBAL)
      set_target_properties (c-ares::cares PROPERTIES IMPORTED_LOCATION ${_cares})
      if (cares_INCLUDE_DIRS)
        set_target_properties (c-ares::cares PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${cares_INCLUDE_DIRS})
      endif ()
      exclude_if_included (c-ares::cares)
    else ()
      message (FATAL_ERROR "using pkg-config for grpc, can't find c-ares")
    endif ()
  else ()

    FetchContent_GetProperties(grpc_src)

    if (NOT grpc_src_POPULATED)
      FetchContent_Populate(
        grpc_src 
        QUIET
        GIT_REPOSITORY https://github.com/grpc/grpc.git
        GIT_TAG v1.28.0
        SOURCE_DIR ${nih_cache_path}/src/grpc_src
        BINARY_DIR ${nih_cache_path}/src/grpc_src-build
        STAMP_DIR ${nih_cache_path}/src/grpc_src-stamp
        TMP_DIR ${nih_cache_path}/tmp)
      
      set (grpc_binary_dir "${grpc_src_BINARY_DIR}")
      set (grpc_source_dir "${grpc_src_SOURCE_DIR}")

      # build options
      set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
      set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
      set(gRPC_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
      set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF CACHE BOOL "" FORCE)
      set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF CACHE BOOL "" FORCE)
      set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF CACHE BOOL "" FORCE)
      set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF CACHE BOOL "" FORCE)
      set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE BOOL "" FORCE)
      set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF CACHE BOOL "" FORCE)
      # cmake fails if package
      set(gRPC_CARES_PROVIDER "module" CACHE STRING "" FORCE)
      set(gRPC_SSL_PROVIDER "package" CACHE STRING "" FORCE)
      set(gRPC_PROTOBUF_PROVIDER "package" CACHE STRING "" FORCE)
      if (${MSVC})
        set(CMAKE_CXX_FLAGS "-GR -Gd -fp:precise -FS -EHa -MP" CACHE STRING "" FORCE)
        set(CMAKE_C_FLAGS "-GR -Gd -fp:precise -FS -MP" CACHE STRING "" FORCE)
      endif()
      
      # AbseilHelpers.cmake build type check checks _build_type for 
      # "static". this conflicts with option static in 
      # RippledSettings.cmake
      set(static_bck ${static})
      unset(static CACHE)
      
      add_subdirectory(${grpc_src_SOURCE_DIR} ${grpc_src_BINARY_DIR})
      
      # set the static back to the saved value
      set(static ${static_bck} CACHE BOOL "link protobuf, openssl, libc++, and boost statically" FORCE)

      exclude_if_included(grpc_src)
    endif()
    set(grpc_cpp_plugin_ "${grpc_binary_dir}/grpc_cpp_plugin")
    set(grpc++_ "grpc++${grpc_suffix}")
  endif()
endif()

set (GRPC_GEN_DIR "${CMAKE_BINARY_DIR}/proto_gen_grpc")
file (MAKE_DIRECTORY ${GRPC_GEN_DIR})
set (GRPC_PROTO_SRCS)
set (GRPC_PROTO_HDRS)
set (GRPC_PROTO_ROOT "${CMAKE_SOURCE_DIR}/src/ripple/proto/org")
file(GLOB_RECURSE GRPC_DEFINITION_FILES LIST_DIRECTORIES false "${GRPC_PROTO_ROOT}/*.proto")
foreach(file ${GRPC_DEFINITION_FILES})
  get_filename_component(_abs_file ${file} ABSOLUTE)
  get_filename_component(_abs_dir ${_abs_file} DIRECTORY)
  get_filename_component(_basename ${file} NAME_WE)
  get_filename_component(_proto_inc ${GRPC_PROTO_ROOT} DIRECTORY) # updir one level
  file(RELATIVE_PATH _rel_root_file ${_proto_inc} ${_abs_file})
  get_filename_component(_rel_root_dir ${_rel_root_file} DIRECTORY)
  file(RELATIVE_PATH _rel_dir ${CMAKE_CURRENT_SOURCE_DIR} ${_abs_dir})

  set (src_1 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.grpc.pb.cc")
  set (src_2 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.pb.cc")
  set (hdr_1 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.grpc.pb.h")
  set (hdr_2 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.pb.h")
  add_custom_command(
    OUTPUT ${src_1} ${src_2} ${hdr_1} ${hdr_2}
    COMMAND protobuf::protoc
    ARGS --grpc_out=${GRPC_GEN_DIR}
         --cpp_out=${GRPC_GEN_DIR}
         --plugin=protoc-gen-grpc=${grpc_cpp_plugin_}
         -I ${_proto_inc} -I ${_rel_dir}
         ${_abs_file}
         DEPENDS ${_abs_file}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running gRPC C++ protocol buffer compiler on ${file}"
    VERBATIM)
    set_source_files_properties(${src_1} ${src_2} ${hdr_1} ${hdr_2} PROPERTIES GENERATED TRUE)
    list(APPEND GRPC_PROTO_SRCS ${src_1} ${src_2})
    list(APPEND GRPC_PROTO_HDRS ${hdr_1} ${hdr_2})
endforeach()

add_library (grpc_pbufs STATIC ${GRPC_PROTO_SRCS} ${GRPC_PROTO_HDRS})
target_include_directories (grpc_pbufs SYSTEM PUBLIC ${GRPC_GEN_DIR})
target_link_libraries (grpc_pbufs ${libprotobuf_} "${grpc++_}")
target_compile_options (grpc_pbufs
  PRIVATE
    $<$<BOOL:${MSVC}>:-wd4065>
    $<$<NOT:$<BOOL:${MSVC}>>:-Wno-deprecated-declarations>
  PUBLIC
    $<$<BOOL:${MSVC}>:-wd4996>
    $<$<BOOL:${is_xcode}>:
      --system-header-prefix="google/protobuf"
      -Wno-deprecated-dynamic-exception-spec
    >)
add_library (Ripple::grpc_pbufs ALIAS grpc_pbufs)
target_link_libraries (ripple_libs INTERFACE Ripple::grpc_pbufs)
target_include_directories(ripple_libs INTERFACE ${grpc_source_dir}/include)
exclude_if_included (grpc_pbufs)
