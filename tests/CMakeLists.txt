include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(queryCreatorTests
    test_qcsqlquery.cpp
    test_qcsqlqueryelement.cpp
    test_qcsqlqueryvalue.cpp
    test_qcsqlinsert.cpp
    test_qcsqlupdate.cpp
    test_qcsqldelete.cpp
    test_qcsqldialect.cpp
    test_qcconnectionpool.cpp
    test_querycreator.cpp
    test_integration_select.cpp
    test_integration_dml.cpp
    testdbconfig.cpp
    testintegrationsupport.cpp
)
target_link_libraries(queryCreatorTests PRIVATE queryCreatorLib GTest::gtest_main)

# Absolute path to this directory, baked in at compile time so
# testdbconfig.cpp can find db_config.ini regardless of the working
# directory ctest invokes the test binary from.
target_compile_definitions(queryCreatorTests PRIVATE QC_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

include(GoogleTest)
gtest_discover_tests(queryCreatorTests)
