# Version and commit from git. The tag vX.Y.Z is the single source of truth;
# before the first tag the version is 0.0.0. Sets CLASS_T4_VERSION and
# CLASS_T4_COMMIT. Both can be passed in with -D to build without git.
if(NOT DEFINED CLASS_T4_VERSION OR NOT DEFINED CLASS_T4_COMMIT)
  find_package(Git QUIET)
endif()

if(NOT DEFINED CLASS_T4_VERSION)
  set(CLASS_T4_VERSION 0.0.0)
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match --match "v[0-9]*.[0-9]*.[0-9]*" HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
      OUTPUT_VARIABLE tag
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE rc)
    if(rc EQUAL 0 AND tag MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
      set(CLASS_T4_VERSION "${CMAKE_MATCH_1}")
    endif()
  endif()
endif()

if(NOT DEFINED CLASS_T4_COMMIT)
  set(CLASS_T4_COMMIT unknown)
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
      OUTPUT_VARIABLE commit
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE rc)
    if(rc EQUAL 0)
      set(CLASS_T4_COMMIT "${commit}")
    endif()
  endif()
endif()

message(STATUS "class-t4 version ${CLASS_T4_VERSION} (${CLASS_T4_COMMIT})")
