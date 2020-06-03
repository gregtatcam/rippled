#[===================================================================[
   import protobuf (lib and compiler) and create a lib
   from our proto message definitions. If the system protobuf
   is not found, fallback on EP to download and build a version
   from official source.
#]===================================================================]

if (static)
  set (Protobuf_USE_STATIC_LIBS ON)
endif ()
find_package (Protobuf 3.8)
if (local_protobuf OR NOT Protobuf_FOUND)
  include (GNUInstallDirs)
  message (STATUS "using local protobuf build.")
  if (WIN32)
    # protobuf prepends lib even on windows
    set (pbuf_lib_pre "lib")
  else ()
    set (pbuf_lib_pre ${ep_lib_prefix})
  endif ()

  FetchContent_GetProperties(protobuf_src)
  if (NOT protobuf_src_POPULATED)
    FetchContent_Populate(
      protobuf_src
      QUIET
      GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
      GIT_TAG v3.8.0
      SOURCE_DIR ${nih_cache_path}/src/protobuf_src
      BINARY_DIR ${nih_cache_path}/src/protobuf_src-build
      STAMP_DIR ${nih_cache_path}/src/protobuf_src-stamp
      TMP_DIR ${nih_cache_path}/tmp)

    set(CMAKE_INSTALL_PREFIX ${protobuf_src_BINARY_DIR}/_installed_ CACHE STRING "" FORCE)
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
    set(protobuf_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CMAKE_DEBUG_POSTFIX "_d" CACHE STRING "" FORCE)
    set(protobuf_DEBUG_POSTFIX "_d" CACHE STRING "" FORCE)
    if (${has_zlib})
      set(protobuf_WITH_ZLIB ${has_zlib} CACHE BOOL "" FORCE)
    else()
      set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
    endif()
    # doesn't work with unity?
    #if (${unity})
    #  set(CMAKE_UNITY_BUILD ON CACHE BOOL "" FORCE)
    #else()
    #  set(CMAKE_UNITY_BUILD OFF CACHE BOOL "" FORCE)
    #endif()
    if (NOT ${is_multiconfig})
      set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE BOOL "" FORCE)
    endif()
    if (${MSVC})
      set(CMAKE_CXX_FLAGS "-GR -Gd -fp:precise -FS -EHa -MP" CACHE STRING "" FORCE)
    endif()

    add_subdirectory(${protobuf_src_SOURCE_DIR}/cmake ${protobuf_src_BINARY_DIR})
    
    set(Protobuf_USE_STATIC_LIBS ${static} CACHE BOOL "" FORCE)
    # need this so gRPC package could find locally installed protobuf. Should export config?
    set(Protobuf_INCLUDE_DIR "${protobuf_src_SOURCE_DIR}/src" CACHE STRING "" FORCE)
    if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
      set(Protobuf_LIBRARY "${protobuf_src_BINARY_DIR}/${pbuf_lib_pre}protobuf_d${ep_lib_suffix}" CACHE STRING "" FORCE)
      set(Protobuf_PROTOC_LIBRARY "${protobuf_src_BINARY_DIR}/${pbuf_lib_pre}protoc_d${ep_lib_suffix}" CACHE STRING "" FORCE)
    else()
      set(Protobuf_LIBRARY "${protobuf_src_BINARY_DIR}/${pbuf_lib_pre}protobuf${ep_lib_suffix}" CACHE STRING "" FORCE)
      set(Protobuf_PROTOC_LIBRARY "${protobuf_src_BINARY_DIR}/${pbuf_lib_pre}protoc${ep_lib_suffix}" CACHE STRING "" FORCE)
    endif()
    set(Protobuf_PROTOC_EXECUTABLE "${protobuf_src_BINARY_DIR}/protoc${CMAKE_EXECUTABLE_SUFFIX}" CACHE STRING "" FORCE)
    # install for use by others;i.e. gRPC
    add_custom_target(
        protobuf_install
        "${CMAKE_COMMAND}" --install . --prefix ./_installed_
        COMMENT "installing protobuf"
        DEPENDS libprotobuf libprotoc protoc
        WORKING_DIRECTORY
            "${protobuf_src_BINARY_DIR}"
    )

    if (NOT TARGET protobuf::libprotobuf)
      add_library (protobuf::libprotobuf STATIC IMPORTED GLOBAL)
    endif ()
    set_target_properties (libprotobuf PROPERTIES
       IMPORTED_LOCATION_DEBUG
         ${protobuf_src_BINARY_DIR}/_installed_/${CMAKE_INSTALL_LIBDIR}/${pbuf_lib_pre}protoc_d${ep_lib_suffix}
       IMPORTED_LOCATION_RELEASE
         ${protobuf_src_BINARY_DIR}/_installed_/${CMAKE_INSTALL_LIBDIR}/${pbuf_lib_pre}protoc${ep_lib_suffix}
       INTERFACE_INCLUDE_DIRECTORIES
         ${protobuf_src_BINARY_DIR}/_installed_/include)

    if (NOT TARGET protobuf::libprotoc)
      add_library (protobuf::libprotoc STATIC IMPORTED GLOBAL)
    endif ()
    set_target_properties (libprotoc PROPERTIES
       IMPORTED_LOCATION_DEBUG
         ${protobuf_src_BINARY_DIR}/_installed_/${CMAKE_INSTALL_LIBDIR}/${pbuf_lib_pre}protoc_d${ep_lib_suffix}
       IMPORTED_LOCATION_RELEASE
         ${protobuf_src_BINARY_DIR}/_installed_/${CMAKE_INSTALL_LIBDIR}/${pbuf_lib_pre}protoc${ep_lib_suffix}
       INTERFACE_INCLUDE_DIRECTORIES
         ${protobuf_src_BINARY_DIR}/_installed_/include)

    if (NOT TARGET protobuf::protoc)
      add_executable (protobuf::protoc IMPORTED)
      exclude_if_included (protobuf::protoc)
    endif ()
    set_target_properties (protoc PROPERTIES
       IMPORTED_LOCATION "${protobuf_src_BINARY_DIR}/_installed_/bin/protoc${CMAKE_EXECUTABLE_SUFFIX}")

  endif()

else ()
  if (NOT TARGET protobuf::protoc)
    if (EXISTS "${Protobuf_PROTOC_EXECUTABLE}")
      add_executable (protobuf::protoc IMPORTED)
      set_target_properties (protobuf::protoc PROPERTIES
        IMPORTED_LOCATION "${Protobuf_PROTOC_EXECUTABLE}")
    else ()
      message (FATAL_ERROR "Protobuf import failed")
    endif ()
  endif ()
  add_custom_target(protobuf_install DEPENDS protobuf::protoc)
endif ()

set(PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/proto_gen")
file (MAKE_DIRECTORY ${PROTO_GEN_DIR})
set(src "${PROTO_GEN_DIR}/ripple.pb.cc")
set(hdr "${PROTO_GEN_DIR}/ripple.pb.h")
set(file "${CMAKE_SOURCE_DIR}/src/ripple/proto/ripple.proto")
get_filename_component(_proto_inc ${file} DIRECTORY)
add_custom_command(
    OUTPUT ${src} ${hdr}
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_GEN_DIR}
         -I ${_proto_inc}
         ${file}
    DEPENDS ${file} protobuf::protoc protobuf_install # have to create the target if not locally installed
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running C++ protocol buffer compiler on ${file}"
    VERBATIM)
set_source_files_properties(${src} ${hdr} PROPERTIES GENERATED TRUE)
add_library (pbufs STATIC ${src} ${hdr})
target_include_directories (pbufs PRIVATE src)
target_include_directories (pbufs SYSTEM PUBLIC ${PROTO_GEN_DIR})
target_link_libraries (pbufs protobuf::libprotobuf)
target_compile_options (pbufs
  PRIVATE
    $<$<BOOL:${MSVC}>:-wd4065>
    $<$<NOT:$<BOOL:${MSVC}>>:-Wno-deprecated-declarations>
  PUBLIC
    $<$<BOOL:${MSVC}>:-wd4996>
    $<$<BOOL:${is_xcode}>:
        --system-header-prefix="google/protobuf"
        -Wno-deprecated-dynamic-exception-spec>
)
add_library (Ripple::pbufs ALIAS pbufs)
add_dependencies(pbufs protobuf::protoc)
target_link_libraries (ripple_libs INTERFACE Ripple::pbufs)
exclude_if_included (pbufs)
