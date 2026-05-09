cmake_minimum_required(VERSION 3.5)
project(UEAdminAPI_drogon CXX)

include(CheckIncludeFileCXX)

check_include_file_cxx(any HAS_ANY)
check_include_file_cxx(string_view HAS_STRING_VIEW)
check_include_file_cxx(coroutine HAS_COROUTINE)
if (NOT "${CMAKE_CXX_STANDARD}" STREQUAL "")
    # Do nothing
elseif (HAS_ANY AND HAS_STRING_VIEW AND HAS_COROUTINE)
    set(CMAKE_CXX_STANDARD 20)
elseif (HAS_ANY AND HAS_STRING_VIEW)
    set(CMAKE_CXX_STANDARD 17)
else ()
    set(CMAKE_CXX_STANDARD 14)
endif ()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
    # 抑制C++17迭代器基类弃用警告
    add_definitions(-D_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
endif()

add_executable(${PROJECT_NAME} main.cc)

# 查找依赖
find_package(Drogon CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon jwt-cpp::jwt-cpp)

# 检查C++标准
if (CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "c++17 or higher is required")
elseif (CMAKE_CXX_STANDARD LESS 20)
    message(STATUS "use c++17")
else ()
    message(STATUS "use c++20")
endif ()

# 收集源文件
aux_source_directory(controllers CTL_SRC)
aux_source_directory(services SERV_SRC)
aux_source_directory(filters FILTER_SRC)
aux_source_directory(plugins PLUGIN_SRC)
aux_source_directory(models MODEL_SRC)
aux_source_directory(utils UTILS_SRC)
aux_source_directory(utils/MFA MFA_SRC)

# 收集对应目录下的头文件（只包含源文件目录下的.h和.hpp）
file(GLOB CTL_HDRS "controllers/*.h" "controllers/*.hpp")
file(GLOB SERV_HDRS "services/*.h" "services/*.hpp")
file(GLOB FILTER_HDRS "filters/*.h" "filters/*.hpp")
file(GLOB PLUGIN_HDRS "plugins/*.h" "plugins/*.hpp")
file(GLOB MODEL_HDRS "models/*.h" "models/*.hpp")
file(GLOB UTILS_HDRS "utils/*.h" "utils/*.hpp")
file(GLOB MFA_HDRS "utils/MFA/*.h" "utils/MFA/*.hpp")
file(GLOB ROOT_HDRS "*.h" "*.hpp")  # 根目录下的头文件（如main.h等）

# 源文件分组（头文件会自动加入对应分组，与源文件放在一起）
source_group("controllers" FILES ${CTL_SRC} ${CTL_HDRS})
source_group("services" FILES ${SERV_SRC} ${SERV_HDRS})
source_group("filters" FILES ${FILTER_SRC} ${FILTER_HDRS})
source_group("plugins" FILES ${PLUGIN_SRC} ${PLUGIN_HDRS})
source_group("models" FILES ${MODEL_SRC} ${MODEL_HDRS})
source_group("utils" FILES ${UTILS_SRC} ${UTILS_HDRS})
source_group("utils/MFA" FILES ${MFA_SRC} ${MFA_HDRS})
source_group("" FILES main.cc ${ROOT_HDRS})  # 根目录文件（主文件+根目录头文件）

# 禁用picojson
target_compile_definitions(${PROJECT_NAME} PRIVATE JWT_DISABLE_PICOJSON)

# 创建视图
drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
                    ${CMAKE_CURRENT_BINARY_DIR})

# 包含目录
target_include_directories(${PROJECT_NAME}
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                   ${CMAKE_CURRENT_SOURCE_DIR}/models)

# 添加所有源文件和头文件到目标
target_sources(${PROJECT_NAME}
               PRIVATE
               ${CTL_SRC} ${CTL_HDRS}
               ${SERV_SRC} ${SERV_HDRS}
               ${FILTER_SRC} ${FILTER_HDRS}
               ${PLUGIN_SRC} ${PLUGIN_HDRS}
               ${MODEL_SRC} ${MODEL_HDRS}
               ${UTILS_SRC} ${UTILS_HDRS}
               ${MFA_SRC} ${MFA_HDRS}
               main.cc ${ROOT_HDRS})

# 动态加载视图（按需启用）
# set_property(TARGET ${PROJECT_NAME} PROPERTY ENABLE_EXPORTS ON)

add_subdirectory(test)