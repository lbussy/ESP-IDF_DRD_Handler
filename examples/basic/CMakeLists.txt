cmake_minimum_required(VERSION 3.16)

# If you want the example to use the component from the local repo
#   export DRD_HANDLER_LOCAL_DEV=1
# And comment the version the example/main/idf_component.yml
if(DEFINED ENV{DRD_HANDLER_LOCAL_DEV} AND "$ENV{DRD_HANDLER_LOCAL_DEV}" STREQUAL "1")
    set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components)
endif()

include(
    $ENV{IDF_PATH}/tools/cmake/project.cmake
)

project(
    drd_handler_basic_example
)
