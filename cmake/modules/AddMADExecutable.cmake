macro(add_mad_executable _name _source_files _libs)

  # build executable as a library to support loading whole thing as a single shared library
  add_library(${_name}-lib EXCLUDE_FROM_ALL ${_source_files})
  foreach(_lib ${_libs})
    set(libtarget ${_lib})
    if (${_lib} MATCHES "^MAD" AND BUILD_SHARED_LIBS AND TARGET ${_lib}-static)
      if (CMAKE_SYSTEM_NAME MATCHES "Linux")
        set(libtarget -Wl,--whole-archive ${_lib}-static -Wl,--no-whole-archive )
      elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
        set(libtarget -Wl,-all_load ${_lib}-static)
      endif()
    endif(${_lib} MATCHES "^MAD" AND BUILD_SHARED_LIBS AND TARGET ${_lib}-static)
    target_link_libraries(${_name}-lib PRIVATE ${libtarget})
  endforeach(_lib "${_libs}")
  add_executable(${_name} EXCLUDE_FROM_ALL ${PROJECT_SOURCE_DIR}/src/madness/misc/exec_stub.cc)
  target_link_libraries(${_name} ${_name}-lib)

endmacro()
