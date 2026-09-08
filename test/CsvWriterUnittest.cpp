#include <gtest/gtest.h>

#include <array>
#include <sstream>
#include <string>

#include "CsvWriter.hpp"

TEST(CsvWriter, WritesUnquotedFields)
{
    std::ostringstream Output;
    CsvWriter Writer(Output);
    const std::array<std::string, 3> Fields = {"Rule", "Matches", "Rate"};

    Writer.WriteRow(Fields);

    EXPECT_EQ(Output.str(), "Rule,Matches,Rate\r\n");
}

TEST(CsvWriter, QuotesAndEscapesSpecialCharacters)
{
    std::ostringstream Output;
    CsvWriter Writer(Output);
    const std::array<std::string, 5> Fields = {
        "plain",
        "comma,value",
        "quote\"value",
        "line\nbreak",
        "carriage\rreturn"
    };

    Writer.WriteRow(Fields);

    EXPECT_EQ(
        Output.str(),
        "plain,\"comma,value\",\"quote\"\"value\",\"line\nbreak\","
        "\"carriage\rreturn\"\r\n"
    );
}

TEST(CsvWriter, PreservesEmptyFields)
{
    std::ostringstream Output;
    CsvWriter Writer(Output);
    const std::array<std::string, 3> Fields = {"", "value", ""};

    Writer.WriteRow(Fields);

    EXPECT_EQ(Output.str(), ",value,\r\n");
}