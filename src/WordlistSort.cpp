//
// Word-list normalization and sorting.
//

#include "WordlistSort.hpp"

#include <algorithm>

#include "Util.hpp"

namespace WordlistSort
{

namespace
{

std::string_view
NormalizeLine(
    const std::string_view Line
)
{
    std::string_view Word = Line;
    if (!Word.empty() && Word.back() == '\r') Word.remove_suffix(1);

    const size_t Separator = Word.find(':');
    if (Separator != std::string_view::npos
        && Util::IsLikelyValidHash(Word.substr(0, Separator)))
    {
        Word.remove_prefix(Separator + 1);
    }

    return Word;
}

template <typename Word>
void
SortWords(
    const std::span<Word> Words,
    const bool LengthFirst
)
{
    std::sort(Words.begin(), Words.end(), [LengthFirst](const std::string_view Left, const std::string_view Right)
    {
        if (LengthFirst && Left.size() != Right.size())
        {
            return Left.size() < Right.size();
        }
        return Left < Right;
    });
}

template <typename Word>
void
WriteWords(
    const std::span<const Word> Words,
    std::ostream& Output,
    const bool Unique,
    const WriteProgress& Progress
)
{
    std::string_view Previous;
    bool HavePrevious = false;
    size_t Processed = 0;
    size_t Written = 0;
    for (const std::string_view Value : Words)
    {
        if (!(Unique && HavePrevious && Value == Previous))
        {
            Output << Util::Hexlify(Value) << '\n';
            if (!Output.good()) break;
            Previous = Value;
            HavePrevious = true;
            ++Written;
        }
        ++Processed;
        if (Progress && Processed % 16384 == 0)
        {
            Progress(Processed, Written);
        }
    }
    if (Progress) Progress(Processed, Written);
}

}

std::string
ParseLine(
    const std::string_view Line
)
{
    return Util::UnHexlify(NormalizeLine(Line));
}

std::string_view
ParseLineView(
    const std::string_view Line,
    std::deque<std::string>& DecodedWords
)
{
    const std::string_view Word = NormalizeLine(Line);
    if (Util::IsHexlified(Word))
    {
        DecodedWords.push_back(Util::UnHexlify(Word));
        return DecodedWords.back();
    }
    return Word;
}

void
Sort(const std::span<std::string> Words, const bool LengthFirst)
{
    SortWords(Words, LengthFirst);
}

void
Sort(const std::span<std::string_view> Words, const bool LengthFirst)
{
    SortWords(Words, LengthFirst);
}

void
Write(const std::span<const std::string> Words, std::ostream& Output, const bool Unique,
    const WriteProgress& Progress)
{
    WriteWords(Words, Output, Unique, Progress);
}

void
Write(const std::span<const std::string_view> Words, std::ostream& Output, const bool Unique,
    const WriteProgress& Progress)
{
    WriteWords(Words, Output, Unique, Progress);
}

}