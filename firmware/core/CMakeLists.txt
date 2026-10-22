cmake_minimum_required(VERSION 3.16.0)

# Shared components (kvm_proto, wifi_sta) live at the repo root;
# core-only components (ddc, ...) are in ./components and found automatically.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deskstrate_core)
