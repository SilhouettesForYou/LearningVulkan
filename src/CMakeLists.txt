cmake_minimum_required(VERSION 3.0)
project(LearningVulkan)

if(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS} -std=c++17 -lglfw3 -lvulkan-1")
endif(CMAKE_COMPILER_IS_GNUCXX)

# Add header file include directories
include_directories(external/imgui)
include_directories(Base)
include_directories(D:\\MinGW\\include)
# include_directories(D:\\MinGW\\include\\um)
include_directories(D:\\MinGW\\include\\shared)
include_directories(D:\\MinGW\\include\\vulkan)
include_directories(D:\\MinGW\\include\\glm)
# include_directories(C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.18362.0\\um)

link_directories(D:\\MinGW\\mingw32\\lib)
# link_directories(C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.18362.0\\um\x\x86)
# Add block directories
add_subdirectory(Base)
add_subdirectory(Triangle)

# Add the source in project root directory
AUX_SOURCE_DIRECTORY(. DIR_SRCS) 

# Target
# add_executable(LearningVulkan ${DIR_SRCS})
# target_link_libraries(Base Triangle)