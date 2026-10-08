cmake_minimum_required(VERSION 3.23)

project(
    AlternateActorsNG
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CommonLibSSE-NG configuration (exported as CommonLibSSE by vcpkg port)
find_package(CommonLibSSE REQUIRED)

add_library(${PROJECT_NAME} SHARED
    src/PCH.h
    src/main.cpp
    src/AliasManager.h
    src/AliasManager.cpp
    src/FaceManager.h
    src/FaceManager.cpp
    src/FactionManager.h
    src/FactionManager.cpp
    src/InventoryManager.h
    src/InventoryManager.cpp
    src/StatManager.h
    src/StatManager.cpp
    src/IdleAIManager.h
    src/IdleAIManager.cpp
    src/Papyrus.h
    src/Papyrus.cpp
    src/Serialization.h
    src/Serialization.cpp
    src/ImpersonationManager.h
    src/ImpersonationManager.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
)

target_precompile_headers(${PROJECT_NAME} PRIVATE
    src/PCH.h
)

# Configure output directories
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "AlternateActorsNG"
    SUFFIX ".dll"
)
