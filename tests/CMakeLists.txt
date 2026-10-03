add_executable(wam_tests test_main.cpp)
target_link_libraries(wam_tests PRIVATE wam_core)
add_test(NAME wam_tests COMMAND wam_tests)

add_executable(wam_flavor_tests flavor_names_test.cpp)
target_link_libraries(wam_flavor_tests PRIVATE wam_core)
add_test(NAME wam_flavor_tests COMMAND wam_flavor_tests)

if(TARGET wam_gui)
    add_executable(wam_gui_tests gui_tests.cpp)
    target_link_libraries(wam_gui_tests PRIVATE wam_gui)
    add_test(NAME wam_gui_tests COMMAND wam_gui_tests)
endif()
