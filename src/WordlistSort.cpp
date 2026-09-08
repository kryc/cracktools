//
// Word-list normalization and sorting.
//

#include "WordlistSort.hpp"

#include <algorithm>

#include "Util.hpp"

namespace WordlistSort
{

std::string
ParseLine(
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

    return Util::UnHexlify(Word);
}

void
Sort(
    const std::span<std::string> Words
)
{
    std::sort(Words.begin(), Words.end());
}

void
Write(
    const std::span<const std::string> Words,
    std::ostream& Output
)
{
    for (const std::string& Word : Words)
    {
        Output << Util::Hexlify(Word) << '\n';
    }
}

}