//
// Hashcat-compatible rule engine.
//

#ifndef Rules_hpp
#define Rules_hpp

#include <cstddef>
#include <string>
#include <string_view>

namespace Rules
{

constexpr size_t MAX_PASSWORD_SIZE = 256;
constexpr size_t MAX_OPERATIONS = 31;

enum class Status
{
    Applied,
    Rejected,
    SyntaxError
};

struct Result
{
    Status status;
    std::string word;
    size_t errorOffset;

    [[nodiscard]] bool Succeeded() const
    {
        return status == Status::Applied;
    }
};

struct CompiledRule
{
    std::string commands;
};

[[nodiscard]]
CompiledRule
Compile(
    const std::string_view Rule
);

// Applies one Hashcat rule line to word. The result contains the candidate as it
// existed when evaluation stopped. Empty and comment-only rule lines are no-ops.
// Position arguments use 0-9 and A-Z (10-35); p uses the position remembered by
// the most recent successful '/', '%', or equivalent character-class reject.
[[nodiscard]]
Result
Apply(
    const std::string_view Input,
    const std::string_view Rule
);

[[nodiscard]]
Result
Apply(
    const std::string_view Input,
    const CompiledRule& Rule
);

}

#endif
