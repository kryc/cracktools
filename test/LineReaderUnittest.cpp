
#include <gtest/gtest.h>


#include "LineReader.hpp"

TEST(LineReader, ReadLine)
{
    LineReader<> reader("../test/LineReaderTestData.txt");
    {
        std::vector<std::string> collected;
        auto line = reader.readLine();
        while (line.has_value())
        {
            collected.emplace_back(line->data(), line->size());
            line = reader.readLine();
        }
        ASSERT_EQ(collected.size(), 9);
        EXPECT_EQ(collected[0], "1");
        EXPECT_EQ(collected[1], "22");
        EXPECT_EQ(collected[8], "999999999");
    }

    // Handle a smaller buffer size
    LineReader<10> readerSmallBuffer("../test/LineReaderTestData.txt");
    {
        std::vector<std::string> collected;
        auto line = readerSmallBuffer.readLine();
        while (line.has_value())
        {
            collected.emplace_back(line->data(), line->size());
            line = readerSmallBuffer.readLine();
        }
        ASSERT_EQ(collected.size(), 9);
        EXPECT_EQ(collected[0], "1");
        EXPECT_EQ(collected[1], "22");
        EXPECT_EQ(collected[8], "999999999");
    }

    // Handle a buffer that lands on a line boundary
    LineReader<8> readerBoundary("../test/LineReaderTestData.txt");
    {
        std::vector<std::string> collected;
        auto line = readerBoundary.readLine();
        while (line.has_value())
        {
            collected.emplace_back(line->data(), line->size());
            line = readerBoundary.readLine();
        }
        ASSERT_EQ(collected.size(), 9);
        EXPECT_EQ(collected[0], "1");
        EXPECT_EQ(collected[1], "22");
        EXPECT_EQ(collected[6], "7777777");
        EXPECT_EQ(collected[7], "88888888");
        EXPECT_EQ(collected[8], "999999999");
    }

    // Handle the other side of the buffer boundary
    LineReader<9> readerOtherBoundary("../test/LineReaderTestData.txt");
    {
        std::vector<std::string> collected;
        auto line = readerOtherBoundary.readLine();
        while (line.has_value())
        {
            collected.emplace_back(line->data(), line->size());
            line = readerOtherBoundary.readLine();
        }
        ASSERT_EQ(collected.size(), 9u);
        EXPECT_EQ(collected[0], "1");
        EXPECT_EQ(collected[1], "22");
        EXPECT_EQ(collected[2], "333");
        EXPECT_EQ(collected[3], "4444");
        EXPECT_EQ(collected[4], "55555");
        EXPECT_EQ(collected[5], "666666");
        EXPECT_EQ(collected[6], "7777777");
        EXPECT_EQ(collected[7], "88888888");
        EXPECT_EQ(collected[8], "999999999");
    }
}
