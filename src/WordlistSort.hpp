//
// Word-list normalization and sorting.
//

#ifndef WordlistSort_hpp
#define WordlistSort_hpp

#include <deque>
#include <functional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace WordlistSort
{

// Called periodically and once at completion with processed and written counts.
using WriteProgress = std::function<void(size_t, size_t)>;

[[nodiscard]]
std::string
ParseLine(
    const std::string_view Line
);

// Non-hex words reference Line; decoded words reference DecodedWords. Both
// backing stores must outlive the returned views. Appending to the deque is safe.
[[nodiscard]]
std::string_view
ParseLineView(
    const std::string_view Line,
    std::deque<std::string>& DecodedWords
);

void
Sort(
    const std::span<std::string> Words,
    const bool LengthFirst = false
);

void
Sort(
    const std::span<std::string_view> Words,
    const bool LengthFirst = false
);

// Words must contain sorted, decoded values. Unique skips adjacent duplicates
// during output, treating plain and hex-encoded spellings of a word equally.
void
Write(
    const std::span<const std::string> Words,
    std::ostream& Output,
    const bool Unique = false,
    const WriteProgress& Progress = {}
);

void
Write(
    const std::span<const std::string_view> Words,
    std::ostream& Output,
    const bool Unique = false,
    const WriteProgress& Progress = {}
);

}

#endif