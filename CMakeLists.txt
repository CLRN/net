cmake_minimum_required(VERSION 3.0)

set(PROJECT_NAME lib_net)
set(CMAKE_CXX_STANDARD 14)

add_subdirectory(log)

set(BOOST_COMPONENTS system iostreams filesystem)
find_package(Boost COMPONENTS ${BOOST_COMPONENTS} REQUIRED)

file(GLOB_RECURSE SOURCES "include/*.hpp")
if(WIN32)
    file(GLOB WIN32_SOURCES "include/net/transports/win32/*.hpp")
    set(SOURCES ${SOURCES} ${WIN32_SOURCES})
else()    
    file(GLOB LINUX_SOURCES "include/net/transports/linux/*.hpp")
    set(SOURCES ${SOURCES} ${LINUX_SOURCES})
endif()


add_library(${PROJECT_NAME} STATIC ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "common/rpc" LINKER_LANGUAGE CXX)

target_include_directories(${PROJECT_NAME} PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(${PROJECT_NAME}
    lib_log
    
    ${Boost_LIBRARIES}
)

if (WITH_TESTS)
    add_subdirectory(tests)
endif()
