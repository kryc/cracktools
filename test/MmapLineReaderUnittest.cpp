#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

#include "LineReader.hpp"

namespace
{
class MmapLineReaderTest : public testing::Test
{
protected:
    void SetUp() override
    {
        static std::atomic<size_t> Next = 0;
        Directory = std::filesystem::temp_directory_path()
            / ("cracktools-mmapreader-" + std::to_string(getpid()) + "-" + std::to_string(Next++));
        std::error_code Error;
        ASSERT_TRUE(std::filesystem::create_directory(Directory, Error));
        ASSERT_FALSE(Error);
    }

    void TearDown() override
    {
        std::error_code Error;
        std::filesystem::remove_all(Directory, Error);
    }

    std::string Add(const std::string_view Contents)
    {
        const auto Path = Directory / std::to_string(NextFile++);
        std::ofstream Output(Path, std::ios::binary);
        Output << Contents;
        Output.close();
        EXPECT_TRUE(Output.good());
        return Path.string();
    }

    std::filesystem::path Directory;
    size_t NextFile = 0;
};
}

TEST_F(MmapLineReaderTest, ClosedOrMissingInputHasNoViewsOrLines)
{
    MmapLineReader Reader;
    EXPECT_FALSE(Reader.Open());
    EXPECT_TRUE(Reader.View().empty());
    EXPECT_TRUE(Reader.IsEof());
    EXPECT_FALSE(Reader.ReadLine());
    EXPECT_FALSE(Reader.SetInputFile((Directory / "missing").string()));
    EXPECT_FALSE(Reader.Open());
    EXPECT_TRUE(Reader.View().empty());
    EXPECT_FALSE(Reader.ReadLine());
    MmapLineReader Missing((Directory / "missing").string());
    EXPECT_FALSE(Missing.Open());
}

TEST_F(MmapLineReaderTest, FullViewIsReadOnlyAndIndependentOfLineCursor)
{
    const std::string Contents = std::string("a\r\nb\nx\0y", 8);
    MmapLineReader Reader(Add(Contents));
    ASSERT_TRUE(Reader.Open());
    const auto View = Reader.View();
    EXPECT_EQ(View, Contents);
    const auto First = Reader.ReadLine();
    ASSERT_TRUE(First);
    EXPECT_EQ(*First, "a\r"); // Preserve the existing LF-only line-reader behavior.
    EXPECT_EQ(First->data(), View.data());
    EXPECT_EQ(Reader.View(), Contents);
    EXPECT_EQ(Reader.ReadLine(), "b");
    const auto Last = Reader.ReadLine();
    ASSERT_TRUE(Last);
    EXPECT_EQ(*Last, std::string("x\0y", 3));
    EXPECT_EQ(Last->data(), View.substr(5).data());
    EXPECT_TRUE(Reader.IsEof());
    EXPECT_FALSE(Reader.ReadLine());
    EXPECT_EQ(Reader.View(), Contents);
}

TEST_F(MmapLineReaderTest, EmptyRegularFileIsOpenAndAlreadyAtEof)
{
    MmapLineReader Reader(Add(""));
    EXPECT_TRUE(Reader.Open());
    EXPECT_TRUE(Reader.View().empty());
    EXPECT_TRUE(Reader.IsEof());
    EXPECT_FALSE(Reader.ReadLine());
}

TEST_F(MmapLineReaderTest, ReopeningResetsCursorAndFailureClearsState)
{
    MmapLineReader Reader(Add("one\ntwo\n"));
    ASSERT_EQ(Reader.ReadLine(), "one");
    ASSERT_TRUE(Reader.SetInputFile(Add("new\nlast")));
    EXPECT_EQ(Reader.View(), "new\nlast");
    EXPECT_EQ(Reader.ReadLine(), "new");
    ASSERT_TRUE(Reader.SetInputFile(Add("")));
    EXPECT_TRUE(Reader.Open());
    EXPECT_TRUE(Reader.View().empty());
    EXPECT_TRUE(Reader.IsEof());
    ASSERT_TRUE(Reader.SetInputFile(Add("again")));
    EXPECT_EQ(Reader.ReadLine(), "again");
    EXPECT_FALSE(Reader.SetInputFile((Directory / "missing").string()));
    EXPECT_FALSE(Reader.Open());
    EXPECT_TRUE(Reader.View().empty());
    EXPECT_FALSE(Reader.ReadLine());
}

TEST_F(MmapLineReaderTest, FilenameCanBeANonNullTerminatedSubview)
{
    const std::string Path = Add("word");
    const std::string Backing = "xx" + Path + ".not-the-file";
    MmapLineReader Reader(std::string_view(Backing).substr(2, Path.size()));
    ASSERT_TRUE(Reader.Open());
    EXPECT_EQ(Reader.View(), "word");
}

TEST_F(MmapLineReaderTest, RejectsDirectoriesAndPipesWithoutBlocking)
{
    MmapLineReader Reader;
    EXPECT_FALSE(Reader.SetInputFile(Directory.string()));
    const auto Pipe = Directory / "pipe";
    ASSERT_EQ(mkfifo(Pipe.c_str(), 0600), 0);
    EXPECT_FALSE(Reader.SetInputFile(Pipe.string()));
    EXPECT_FALSE(Reader.Open());
    EXPECT_TRUE(Reader.View().empty());
}

TEST_F(MmapLineReaderTest, ReopeningCanUseAPathViewFromTheCurrentMapping)
{
    const std::string Target = Add("target contents");
    MmapLineReader Reader(Add(Target));
    ASSERT_TRUE(Reader.Open());
    ASSERT_TRUE(Reader.SetInputFile(Reader.View()));
    EXPECT_EQ(Reader.View(), "target contents");
}

TEST_F(MmapLineReaderTest, MoveTransfersOwnershipViewsAndCursor)
{
    static_assert(!std::is_copy_constructible_v<MmapLineReader>);
    static_assert(!std::is_copy_assignable_v<MmapLineReader>);
    static_assert(std::is_nothrow_move_constructible_v<MmapLineReader>);
    static_assert(std::is_nothrow_move_assignable_v<MmapLineReader>);
    MmapLineReader Source(Add("one\ntwo"));
    ASSERT_EQ(Source.ReadLine(), "one");
    const auto View = Source.View();
    MmapLineReader Moved(std::move(Source));
    EXPECT_FALSE(Source.Open());
    EXPECT_TRUE(Source.View().empty());
    EXPECT_FALSE(Source.ReadLine());
    EXPECT_EQ(Moved.View().data(), View.data());
    EXPECT_EQ(View, "one\ntwo");
    MmapLineReader Destination(Add("other"));
    Destination = std::move(Moved);
    EXPECT_FALSE(Moved.Open());
    EXPECT_TRUE(Moved.View().empty());
    EXPECT_EQ(Destination.View().data(), View.data());
    EXPECT_EQ(Destination.ReadLine(), "two");
    EXPECT_TRUE(Destination.IsEof());
}

TEST_F(MmapLineReaderTest, EmptyAndClosedReadersCanBeMoved)
{
    MmapLineReader Empty(Add(""));
    MmapLineReader Moved(std::move(Empty));
    EXPECT_TRUE(Moved.Open());
    EXPECT_TRUE(Moved.View().empty());
    EXPECT_FALSE(Empty.Open());
    MmapLineReader Closed;
    Moved = std::move(Closed);
    EXPECT_FALSE(Moved.Open());
    EXPECT_TRUE(Moved.View().empty());
}