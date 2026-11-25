option(ELMASDS_BUILD_E2E_TESTS "Build ElmasDS opaque-box E2E tests" OFF)

target_include_directories(
    elmasds_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_sources(
    elmasds_tests
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/nds_rom_builder.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/arm_program_emitter.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/system_image_builder.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/test_e2e_builders.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/test_e2e_builder_edge_cases.cpp"
)

if(ELMASDS_BUILD_E2E_TESTS)
    add_executable(
        elmasds_e2e_tests
        "${CMAKE_CURRENT_LIST_DIR}/emulator_e2e_harness.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/nds_rom_builder.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/arm_program_emitter.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/system_image_builder.cpp"
    )

    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/e2e_m2.cmake")
        include("${CMAKE_CURRENT_LIST_DIR}/e2e_m2.cmake")
    endif()
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/e2e_m3.cmake")
        include("${CMAKE_CURRENT_LIST_DIR}/e2e_m3.cmake")
    endif()
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/e2e_m4.cmake")
        include("${CMAKE_CURRENT_LIST_DIR}/e2e_m4.cmake")
    endif()

    target_include_directories(
        elmasds_e2e_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_link_libraries(
        elmasds_e2e_tests
        PRIVATE
            elmasds_core
            GTest::gtest_main
    )

    elmasds_apply_warnings(elmasds_e2e_tests)

    gtest_discover_tests(elmasds_e2e_tests)
endif()
