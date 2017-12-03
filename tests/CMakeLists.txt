set(PROJECT_NAME net_tests)

find_package(GTest REQUIRED)
find_package(GMock REQUIRED)

file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "*.h")


add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "common/tests")
target_link_libraries(${PROJECT_NAME}
	lib_net

    ${GTEST_BOTH_LIBRARIES}
    ${GMOCK_BOTH_LIBRARIES}
)
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})