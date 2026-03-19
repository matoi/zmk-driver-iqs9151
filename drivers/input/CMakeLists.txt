zephyr_library_amend()

set(_zmk_app_include_candidates
  ${ZEPHYR_BASE}/../app/include
  ${ZEPHYR_BASE}/../zmk/app/include
)
foreach(_inc ${_zmk_app_include_candidates})
  if(EXISTS ${_inc})
    zephyr_library_include_directories(${_inc})
    break()
  endif()
endforeach()
zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../include)

zephyr_library_sources_ifdef(CONFIG_INPUT_IQS9151 iqs9151.c)
