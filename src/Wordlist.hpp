#ifndef Wordlist_hpp
#define Wordlist_hpp

#include <filesystem>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace Wordlist
{

enum class Operation
{
    Union, Difference, Intersection, SymmetricDifference, Lower, Upper, Title, Clean
};

struct Options
{
    Operation Action = Operation::Union;
    // Zero selects hardware concurrency (with a fallback of one worker).
    size_t Threads = 0;
    bool IncludeComments = false;
};

std::optional<Operation> ParseOperation(std::string_view Name);

// In-memory processing of regular files. Inputs must not change during the call.
// ASCII casing only; other bytes are preserved. Output uses Util::Hexlify rules.
// TODO(ICU): Unicode decoding, case mapping/titlecasing, and transliteration.
// On failure, Error describes the problem. Output may be partial on write errors.
[[nodiscard]] bool Process(std::span<const std::filesystem::path> Files,
    const Options& Settings, std::ostream& Output, std::string& Error);

}

#endif