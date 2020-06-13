
#include <gtest/gtest.h>
#include <packio/packio.h>

int main(int argc, char** argv)
{
#if defined(PACKIO_LOGGING)
    ::spdlog::default_logger()->set_level(
        static_cast<::spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
#endif
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
