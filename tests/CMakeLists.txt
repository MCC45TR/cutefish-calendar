include(GoogleTest)

# The model alone: no toolkit, so no window can come in.
add_executable(calendar-logic-tests
    logic/main.cpp
    logic/core_test.cpp
    logic/reminders_test.cpp
    ${PROJECT_SOURCE_DIR}/src/reminderservice.cpp
)
target_link_libraries(calendar-logic-tests PRIVATE calendar-core GTest::gtest)
gtest_discover_tests(calendar-logic-tests PROPERTIES LABELS logic)

add_executable(calendar-scenario-tests
    scenarios/main.cpp
    scenarios/calendars_test.cpp
    scenarios/editor_test.cpp
    scenarios/events_test.cpp
    scenarios/views_test.cpp
)
target_link_libraries(calendar-scenario-tests PRIVATE calendar-ui GTest::gtest)
gtest_discover_tests(calendar-scenario-tests PROPERTIES LABELS scenario)

add_executable(calendar-layout-tests layout/layout_test.cpp)
target_link_libraries(calendar-layout-tests PRIVATE calendar-ui GTest::gtest)
gtest_discover_tests(calendar-layout-tests PROPERTIES LABELS layout)

add_executable(calendar-capture layout/capture.cpp)
target_link_libraries(calendar-capture PRIVATE calendar-ui)
