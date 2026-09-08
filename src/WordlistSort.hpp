//
// Word-list normalization and sorting.
//

#ifndef WordlistSort_hpp
#define WordlistSort_hpp

#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace WordlistSort
{

[[nodiscard]]
std::string
ParseLine(
    const std::string_view Line
);

void
Sort(
    const std::span<std::string> Words
);

void
Write(
    const std::span<const std::string> Words,
    std::ostream& Output
);

}

#endif