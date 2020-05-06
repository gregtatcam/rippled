# currently linking to unsecure versions...if we switch, we'll
# need to add ssl as a link dependency to the grpc targets
option (use_secure_grpc "use TLS version of grpc libs." OFF)
if (use_secure_grpc)
  set (grpc_suffix "")
else ()
  set (grpc_suffix "_unsecure")
endif ()

#add_executable (gRPC::grpc_cpp_plugin IMPORTED)
#exclude_if_included (gRPC::grpc_cpp_plugin)

# AbseilHelpers.cmake build type check NEED TO FIX
#set(${_build_type} "static" CACHE STRING "" FORCE)

FetchContent_GetProperties(grpc_src)
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

set(CMAKE_CXX_COMPILER ${CMAKE_CXX_COMPILER})
set(CMAKE_C_COMPILER ${CMAKE_C_COMPILER})
set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF CACHE BOOL "" FORCE)
#set(werr OFF CACHE BOOL "" FORCE)
#set(CMAKE_CXX_FLAGS "-Wno-error=ignored-qualifiers -w -Wno-ignored-qualifiers")
add_definitions("-Wno-error=ignored-qualifiers -w -Wno-ignored-qualifiers")
add_subdirectory(${grpc_src_SOURCE_DIR} ${grpc_src_BINARY_DIR})

file (MAKE_DIRECTORY ${grpc_src_SOURCE_DIR}/include)

set (GRPC_GEN_DIR "${CMAKE_BINARY_DIR}/proto_gen_grpc")
file (MAKE_DIRECTORY ${GRPC_GEN_DIR})
set (GRPC_PROTO_SRCS)
set (GRPC_PROTO_HDRS)
set (GRPC_PROTO_ROOT "${CMAKE_SOURCE_DIR}/src/ripple/proto/org")
file(GLOB_RECURSE GRPC_DEFINITION_FILES LIST_DIRECTORIES false "${GRPC_PROTO_ROOT}/*.proto")

### protocol messag
set (PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/proto_gen")
file (MAKE_DIRECTORY ${PROTO_GEN_DIR})
set(file ${CMAKE_SOURCE_DIR}/src/ripple/proto/ripple.proto)
get_filename_component(_proto_inc ${file} DIRECTORY) # updir one level
set(src ${PROTO_GEN_DIR}/ripple.pb.cc)
set(hdr ${PROTO_GEN_DIR}/ripple.pb.h)
add_custom_command(
    OUTPUT ${src} ${hdr}
    COMMAND $<TARGET_FILE:protoc>
    ARGS --cpp_out=${PROTO_GEN_DIR}
         -I ${_proto_inc}
         ${file}
    DEPENDS ${file}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running C++ protocol buffer compiler on ripple.proto"
    VERBATIM)
set_source_files_properties(proto.pb.cc proto.pb.h PROPERTIES GENERATED TRUE)
add_library (pbufs STATIC ${src} ${hdr})
target_include_directories (pbufs PRIVATE src)
target_include_directories (pbufs
  SYSTEM PUBLIC ${CMAKE_BINARY_DIR}/proto_gen)
target_link_libraries (pbufs libprotobuf)
target_compile_options (pbufs
  PUBLIC
    $<$<BOOL:${is_xcode}>:
      --system-header-prefix="google/protobuf"
      -Wno-deprecated-dynamic-exception-spec
    >)
add_library (Ripple::pbufs ALIAS pbufs)
target_link_libraries (ripple_libs INTERFACE Ripple::pbufs)
exclude_if_included (pbufs)
### end protocol message

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
    COMMAND $<TARGET_FILE:protoc>
    ARGS --grpc_out=${GRPC_GEN_DIR}
         --cpp_out=${GRPC_GEN_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
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
#target_include_directories (grpc_pbufs PRIVATE src)
target_include_directories (grpc_pbufs SYSTEM PUBLIC ${GRPC_GEN_DIR})
target_link_libraries (grpc_pbufs libprotobuf grpc++${grpc_suffix})
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
target_include_directories(ripple_libs INTERFACE ${nih_cache_path}/src/grpc_src/include)
exclude_if_included (grpc_pbufs)
