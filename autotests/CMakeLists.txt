find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

ecm_add_test(
    DisplayControlTest.cpp
    TEST_NAME displaycontroltest
    LINK_LIBRARIES KRdp Qt6::Test
)

ecm_add_test(
    ClipboardTest.cpp
    TEST_NAME clipboardtest
    LINK_LIBRARIES KRdp Qt6::Test
)
target_include_directories(clipboardtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
