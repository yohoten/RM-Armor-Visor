# CMakeLists.txt - 装甲板识别系统构建配置
cmake_minimum_required(VERSION 3.10)
project(ArmorDetector)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置构建类型
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 编译器优化选项
if(MSVC)
    # 添加UTF-8支持和异常处理以解决编码和异常处理问题
    set(CMAKE_CXX_FLAGS "/W3 /utf-8 /EHsc")
    set(CMAKE_CXX_FLAGS_DEBUG "/MTd /Zi")
    set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2")
else()
    set(CMAKE_CXX_FLAGS "-Wall -Wextra")
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

# 修正OpenCV_DIR路径 - MinGW OpenCV 4.5.2
set(OpenCV_DIR "H:/OpenCV-MinGW-Build-OpenCV-4.5.2-x64")

# 查找OpenCV包
find_package(OpenCV REQUIRED)
if(NOT OpenCV_FOUND)
    message(FATAL_ERROR
        "OpenCV 4 not found. Please set OpenCV_DIR to the directory containing OpenCVConfig.cmake.\n"
        "For Windows: set OpenCV_DIR to 'C:/opencv/build/x64/vc15/lib'.\n"
        "Example: cmake -DOpenCV_DIR=\"C:/opencv/build/x64/vc15/lib\" ..")
endif()

# 包含头文件目录
include_directories(${CMAKE_SOURCE_DIR}/src 
                    ${CMAKE_SOURCE_DIR}/third_party
                    ${OpenCV_INCLUDE_DIRS})

# 添加可执行文件
add_executable(armor_detector 
    src/main.cpp 
    src/core/ImageProcessor.cpp
    src/camera/CameraManager.cpp
    src/ui/UIController.cpp
    src/utils/ConfigManager.cpp
)

# 链接OpenCV库
# Modified: 移除手动指定的库文件，完全依赖find_package自动发现的${OpenCV_LIBS}
target_link_libraries(armor_detector PRIVATE ${OpenCV_LIBS})

# 添加OpenCV的bin目录到可执行文件的搜索路径
# 这样可以确保运行时能找到OpenCV的DLL文件
if(WIN32)
    # 尝试找到OpenCV的bin目录 (支持MSVC和MinGW两种构建)
    set(OpenCV_BIN_DIR "${OpenCV_DIR}/x64/mingw/bin")
    if(NOT EXISTS "${OpenCV_BIN_DIR}")
        get_filename_component(OpenCV_BIN_DIR "${OpenCV_DIR}/../../../x64/vc15/bin" ABSOLUTE)
    endif()

    if(EXISTS "${OpenCV_BIN_DIR}")
        add_custom_command(TARGET armor_detector POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OpenCV_BIN_DIR}"
            $<TARGET_FILE_DIR:armor_detector>
            COMMENT "Copying OpenCV DLLs from ${OpenCV_BIN_DIR} to output directory"
        )
    else()
        message(WARNING "OpenCV bin directory not found at ${OpenCV_BIN_DIR}. DLLs will not be copied automatically.")
    endif()

    # Also copy MSYS2 mingw64 runtime DLLs if available
    set(MSYS2_MINGW64_BIN "C:/msys64/mingw64/bin")
    if(EXISTS "${MSYS2_MINGW64_BIN}")
        add_custom_command(TARGET armor_detector POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MSYS2_MINGW64_BIN}/libgcc_s_seh-1.dll"
            "${MSYS2_MINGW64_BIN}/libstdc++-6.dll"
            "${MSYS2_MINGW64_BIN}/libwinpthread-1.dll"
            $<TARGET_FILE_DIR:armor_detector>
            COMMENT "Copying MSYS2 MSVCRT runtime DLLs"
        )
    endif()
endif()