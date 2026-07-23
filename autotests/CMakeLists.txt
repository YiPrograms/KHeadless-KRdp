find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

ecm_add_test(
    DisplayControlTest.cpp
    TEST_NAME displaycontroltest
    LINK_LIBRARIES KRdp Qt6::Test
)
