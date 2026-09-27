add_executable(wam_tests test_main.cpp)
target_link_libraries(wam_tests PRIVATE wam_core)
add_test(NAME wam_tests COMMAND wam_tests)
