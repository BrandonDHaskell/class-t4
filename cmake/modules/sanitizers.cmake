# AddressSanitizer and UndefinedBehaviorSanitizer for the host-debug preset.
# Applied to project targets only; vcpkg dependencies are built without them.
option(CLASS_T4_SANITIZERS "Build project targets with ASan and UBSan" OFF)

function(class_t4_set_sanitizers target)
  if(NOT CLASS_T4_SANITIZERS)
    return()
  endif()
  set(flags -fsanitize=address,undefined -fno-sanitize-recover=undefined
            -fno-omit-frame-pointer)
  target_compile_options(${target} PRIVATE ${flags})
  target_link_options(${target} PRIVATE ${flags})
endfunction()
