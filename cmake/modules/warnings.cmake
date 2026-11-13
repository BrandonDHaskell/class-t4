# Project warning set, applied per target so vcpkg dependencies are untouched.
option(CLASS_T4_WERROR "Treat warnings as errors (CI turns this on)" OFF)

function(class_t4_set_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    $<$<BOOL:${CLASS_T4_WERROR}>:-Werror>)
endfunction()
