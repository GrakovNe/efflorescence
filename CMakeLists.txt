cmake_minimum_required(VERSION 3.16)

# A release build is told its version; any other build takes what git
# describe says, which is never a release number.
if(DEFINED ENV{APP_VERSION})
    set(PROJECT_VER "$ENV{APP_VERSION}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(efflorescence)
