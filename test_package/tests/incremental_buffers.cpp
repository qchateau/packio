
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <packio/nl_json_rpc/incremental_buffers.h>

using namespace packio::nl_json_rpc;

class TestParser : public ::testing::Test {
};

TEST(TestParser, test_simple_object)
{
    incremental_buffers parser;
    ASSERT_FALSE(parser.get_parsed_buffer());

    const nlohmann::json obj = {
        {"key", 42},
        {"an\"noy}i{ngkey{", "ann\"yi\\\"ngvalue{}}{"},
        {"nested", {"key", 12}},
    };
    parser.feed(obj.dump());
    auto buffer = parser.get_parsed_buffer();
    ASSERT_TRUE(buffer);
    ASSERT_EQ(nlohmann::json::parse(*buffer), obj);

    parser.feed(obj.dump(2));
    buffer = parser.get_parsed_buffer();
    ASSERT_TRUE(buffer);
    ASSERT_EQ(nlohmann::json::parse(*buffer), obj);
}

TEST(TestParser, test_simple_array)
{
    incremental_buffers parser;
    ASSERT_FALSE(parser.get_parsed_buffer());

    const nlohmann::json obj = {
        "value1",
        42,
        "ann\"yi\\\"ngvalue{}}{",
        {"nested", {"key", 12}},
    };
    parser.feed(obj.dump());
    auto buffer = parser.get_parsed_buffer();
    ASSERT_TRUE(buffer);
    ASSERT_EQ(nlohmann::json::parse(*buffer), obj);

    parser.feed(obj.dump(2));
    buffer = parser.get_parsed_buffer();
    ASSERT_TRUE(buffer);
    ASSERT_EQ(nlohmann::json::parse(*buffer), obj);
}

TEST(TestParser, test_multiple_object)
{
    incremental_buffers parser;
    ASSERT_FALSE(parser.get_parsed_buffer());

    const int n_feed = 5;
    const nlohmann::json obj = {{"key", 42}, {"nested", {"key", 12}}};
    for (int i = 0; i < n_feed; ++i) {
        parser.feed(obj.dump(i));
    }

    for (int i = 0; i < n_feed; ++i) {
        auto buffer = parser.get_parsed_buffer();
        ASSERT_TRUE(buffer);
        ASSERT_EQ(nlohmann::json::parse(*buffer), obj);
    }
}

TEST(TestParser, test_partial_feed)
{
    incremental_buffers parser;
    ASSERT_FALSE(parser.get_parsed_buffer());

    const nlohmann::json obj = {{"key", 42}, {"nested", {"key", 12}}};
    const std::string serialized = obj.dump();

    const std::size_t middle_pos = serialized.size() / 2;
    const std::string part1 = serialized.substr(0, middle_pos);
    const std::string part2 = serialized.substr(middle_pos);

    parser.feed(part1);
    ASSERT_FALSE(parser.get_parsed_buffer());

    parser.feed(part2);
    auto buffer = parser.get_parsed_buffer();
    ASSERT_TRUE(buffer);
    ASSERT_EQ(nlohmann::json::parse(*buffer), obj);
}

TEST(TestParser, test_partial_feed_complex)
{
    incremental_buffers parser;
    ASSERT_FALSE(parser.get_parsed_buffer());

    const int n_feed = 5;
    const nlohmann::json obj = {{"key", 42}, {"nested", {"key", 12}}};
    const std::string serialized = obj.dump();
    const std::size_t feed_size = serialized.size() - 3;
    std::string feed_buffer;
    for (int i = 0; i < n_feed; ++i) {
        feed_buffer += serialized;
    }

    std::size_t pos = 0;
    while (pos < serialized.size()) {
        const std::string chunk = feed_buffer.substr(pos, feed_size);

        parser.feed(chunk);
        if (pos == 0) {
            ASSERT_FALSE(parser.get_parsed_buffer());
        }
        else {
            auto buffer = parser.get_parsed_buffer();
            ASSERT_TRUE(buffer);
            ASSERT_EQ(nlohmann::json::parse(*buffer), obj);
        }

        pos += feed_size;
    }
}
