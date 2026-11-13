# Flags that keep build paths and toolchain runtime out of the artifact.
# Release binaries are also stripped and statically link libstdc++/libgcc,
# so the only NEEDED entries are glibc libraries.
function(class_t4_set_reproducible target)
  target_compile_options(${target} PRIVATE
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
  target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc
    -Wl,--build-id=sha1
    $<$<CONFIG:Release>:-s>)
endfunction()
