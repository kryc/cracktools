//
// Hashcat-compatible rule engine.
//

#include "Rules.hpp"

#include "Util.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Rules
{
namespace
{

constexpr size_t NO_ERROR = std::string_view::npos;

const bool
IsSymbol(
    const char Value
)
{
    const auto byte = static_cast<unsigned char>(Value);
    return byte == ' '
        || (byte >= '!' && byte <= '/')
        || (byte >= ':' && byte <= '@')
        || (byte >= '[' && byte <= '`')
        || (byte >= '{' && byte <= '~');
}

const bool
IsClass(
    const char Value,
    const char CharacterClass
)
{
    switch (CharacterClass)
    {
        case 'l': return ::Util::IsLower(Value);
        case 'u': return ::Util::IsUpper(Value);
        case 'd': return ::Util::IsNumeric(Value);
        case 'h': return ::Util::IsLowerHex(Value);
        case 'H': return ::Util::IsUpperHex(Value);
        case 's': return IsSymbol(Value);
        case '?': return Value == '?';
        default: return false;
    }
}

const bool
IsValidClass(
    const char Value
)
{
    return Value == 'l' || Value == 'u' || Value == 'd'
        || Value == 'h' || Value == 'H' || Value == 's' || Value == '?';
}

std::string
DecodeHexNotation(
    const std::string_view Rule
)
{
    std::string decoded;
    decoded.reserve(Rule.size());

    for (size_t i = 0; i < Rule.size(); ++i)
    {
        if (Rule[i] == '\\' && i + 3 < Rule.size() && Rule[i + 1] == 'x'
            && ::Util::IsHex(Rule[i + 2]) && ::Util::IsHex(Rule[i + 3]))
        {
            decoded.push_back(static_cast<char>(::Util::ParseHexUint16(Rule.substr(i + 2, 2))));
            i += 3;
        }
        else
        {
            decoded.push_back(Rule[i]);
        }
    }

    return decoded;
}

const bool
IsIgnoredWhitespace(
    const char Value
)
{
    return Value == ' ' || Value == '\t' || Value == '\r';
}

std::optional<size_t>
ParsePosition(
    const char Value,
    const std::optional<size_t> SavedPosition
)
{
    if (Value == 'p') return SavedPosition;
    if (Value >= '0' && Value <= '9') return static_cast<size_t>(Value - '0');
    if (::Util::IsUpper(Value)) return static_cast<size_t>(Value - 'A' + 10);
    return std::nullopt;
}

void
Lowercase(
    std::string& Word
)
{
    Word = ::Util::ToLower(Word);
}

void
Uppercase(
    std::string& Word
)
{
    Word = ::Util::ToUpper(Word);
}

void
Title(
    std::string& Word,
    const char Separator
)
{
    bool upperNext = true;
    for (char& value : Word)
    {
        if (value == Separator)
        {
            upperNext = true;
            continue;
        }
        value = upperNext ? ::Util::ToUpper(value) : ::Util::ToLower(value);
        upperNext = false;
    }
}

void
TitleClass(
    std::string& Word,
    const char CharacterClass
)
{
    bool upperNext = true;
    for (char& value : Word)
    {
        const bool separator = IsClass(value, CharacterClass);
        if (separator)
        {
            upperNext = true;
            continue;
        }
        value = upperNext ? ::Util::ToUpper(value) : ::Util::ToLower(value);
        upperNext = false;
    }
}

std::optional<size_t>
FindClass(
    const std::string_view Word,
    const char CharacterClass
)
{
    for (size_t i = 0; i < Word.size(); ++i)
    {
        if (IsClass(Word[i], CharacterClass)) return i;
    }
    return std::nullopt;
}

template <typename Predicate>
std::optional<size_t>
FindOccurrence(
    const std::string_view Word,
    const size_t Count,
    const Predicate& PredicateFunction
)
{
    if (Count == 0) return std::nullopt;

    size_t found = 0;
    for (size_t i = 0; i < Word.size(); ++i)
    {
        if (!PredicateFunction(Word[i])) continue;
        ++found;
        if (found == Count) return i;
    }
    return std::nullopt;
}

Result
SyntaxError(
    std::string Word,
    const size_t Offset
)
{
    return {Status::SyntaxError, std::move(Word), Offset};
}

Result
Rejected(
    std::string Word,
    const size_t Offset
)
{
    return {Status::Rejected, std::move(Word), Offset};
}

}

CompiledRule
Compile(
    const std::string_view Rule
)
{
    const size_t First = Rule.find_first_not_of(" \t\r");
    if (First == std::string_view::npos || Rule[First] == '#') return {};

    return {DecodeHexNotation(Rule)};
}

Result
Apply(
    const std::string_view Input,
    const std::string_view Rule
)
{
    return Apply(Input, Compile(Rule));
}

Result
Apply(
    const std::string_view Input,
    const CompiledRule& Rule
)
{
    if (Input.size() > MAX_PASSWORD_SIZE) return SyntaxError(std::string(Input), 0);

    if (Rule.commands.empty())
    {
        return {Status::Applied, std::string(Input), NO_ERROR};
    }

    const std::string_view commands = Rule.commands;
    std::string word(Input);
    std::string memory;
    std::optional<size_t> savedPosition;
    size_t operationCount = 0;

    auto readCharacter = [&](size_t& offset) -> std::optional<char>
    {
        if (++offset >= commands.size()) return std::nullopt;
        return commands[offset];
    };

    auto readPosition = [&](size_t& offset) -> std::optional<size_t>
    {
        const auto value = readCharacter(offset);
        if (!value) return std::nullopt;
        return ParsePosition(*value, savedPosition);
    };

    for (size_t i = 0; i < commands.size(); ++i)
    {
        if (IsIgnoredWhitespace(commands[i])) continue;

        const size_t commandOffset = i;
        if (++operationCount > MAX_OPERATIONS) return SyntaxError(std::move(word), commandOffset);

        switch (commands[i])
        {
            case ':':
                break;

            case 'l':
                Lowercase(word);
                break;

            case 'u':
                Uppercase(word);
                break;

            case 'c':
                Lowercase(word);
                if (!word.empty()) word.front() = ::Util::ToUpper(word.front());
                break;

            case 'C':
                Uppercase(word);
                if (!word.empty()) word.front() = ::Util::ToLower(word.front());
                break;

            case 't':
                for (char& value : word) value = ::Util::ToggleCase(value);
                break;

            case 'T':
            {
                const auto position = readPosition(i);
                if (!position) return SyntaxError(std::move(word), commandOffset);
                if (*position < word.size()) word[*position] = ::Util::ToggleCase(word[*position]);
                break;
            }

            case 'r':
                std::reverse(word.begin(), word.end());
                break;

            case 'd':
                if (word.size() * 2 < MAX_PASSWORD_SIZE) word += word;
                break;

            case 'p':
            {
                const auto count = readPosition(i);
                if (!count) return SyntaxError(std::move(word), commandOffset);
                const std::string original = word;
                if (original.size() * (*count + 1) < MAX_PASSWORD_SIZE)
                {
                    for (size_t n = 0; n < *count; ++n) word += original;
                }
                break;
            }

            case 'f':
                if (word.size() * 2 < MAX_PASSWORD_SIZE)
                {
                    const std::string original = word;
                    word.append(original.rbegin(), original.rend());
                }
                break;

            case '{':
                if (!word.empty()) std::rotate(word.begin(), word.begin() + 1, word.end());
                break;

            case '}':
                if (!word.empty()) std::rotate(word.begin(), word.end() - 1, word.end());
                break;

            case '$':
            {
                const auto value = readCharacter(i);
                if (!value) return SyntaxError(std::move(word), commandOffset);
                if (word.size() + 1 < MAX_PASSWORD_SIZE) word.push_back(*value);
                break;
            }

            case '^':
            {
                const auto value = readCharacter(i);
                if (!value) return SyntaxError(std::move(word), commandOffset);
                if (word.size() + 1 < MAX_PASSWORD_SIZE) word.insert(word.begin(), *value);
                break;
            }

            case '[':
                if (!word.empty()) word.erase(word.begin());
                break;

            case ']':
                if (!word.empty()) word.pop_back();
                break;

            case 'D':
            {
                const auto position = readPosition(i);
                if (!position) return SyntaxError(std::move(word), commandOffset);
                if (*position < word.size()) word.erase(*position, 1);
                break;
            }

            case 'x':
            {
                const auto position = readPosition(i);
                const auto length = readPosition(i);
                if (!position || !length) return SyntaxError(std::move(word), commandOffset);
                if (*position < word.size() && *position + *length <= word.size())
                {
                    word = word.substr(*position, *length);
                }
                break;
            }

            case 'O':
            {
                const auto position = readPosition(i);
                const auto length = readPosition(i);
                if (!position || !length) return SyntaxError(std::move(word), commandOffset);
                if (*position < word.size() && *position + *length <= word.size())
                {
                    word.erase(*position, *length);
                }
                break;
            }

            case 'i':
            {
                const auto position = readPosition(i);
                const auto value = readCharacter(i);
                if (!position || !value) return SyntaxError(std::move(word), commandOffset);
                if (*position <= word.size() && word.size() + 1 < MAX_PASSWORD_SIZE)
                {
                    word.insert(word.begin() + static_cast<std::string::difference_type>(*position), *value);
                }
                break;
            }

            case 'o':
            {
                const auto position = readPosition(i);
                const auto value = readCharacter(i);
                if (!position || !value) return SyntaxError(std::move(word), commandOffset);
                if (*position < word.size()) word[*position] = *value;
                break;
            }

            case '\'':
            {
                const auto position = readPosition(i);
                if (!position) return SyntaxError(std::move(word), commandOffset);
                if (*position < word.size()) word.resize(*position);
                break;
            }

            case 's':
            {
                const auto oldValue = readCharacter(i);
                const auto newValue = readCharacter(i);
                if (!oldValue || !newValue) return SyntaxError(std::move(word), commandOffset);
                std::replace(word.begin(), word.end(), *oldValue, *newValue);
                break;
            }

            case '@':
            {
                const auto value = readCharacter(i);
                if (!value) return SyntaxError(std::move(word), commandOffset);
                word.erase(std::remove(word.begin(), word.end(), *value), word.end());
                break;
            }

            case 'z':
            {
                const auto count = readPosition(i);
                if (!count) return SyntaxError(std::move(word), commandOffset);
                if (!word.empty() && word.size() + *count < MAX_PASSWORD_SIZE)
                {
                    word.insert(0, *count, word.front());
                }
                break;
            }

            case 'Z':
            {
                const auto count = readPosition(i);
                if (!count) return SyntaxError(std::move(word), commandOffset);
                if (!word.empty() && word.size() + *count < MAX_PASSWORD_SIZE)
                {
                    word.append(*count, word.back());
                }
                break;
            }

            case 'q':
                if (!word.empty() && word.size() * 2 < MAX_PASSWORD_SIZE)
                {
                    std::string duplicated;
                    duplicated.reserve(word.size() * 2);
                    for (const char value : word)
                    {
                        duplicated.push_back(value);
                        duplicated.push_back(value);
                    }
                    word = std::move(duplicated);
                }
                break;

            case 'X':
            {
                const auto memoryPosition = readPosition(i);
                const auto length = readPosition(i);
                const auto wordPosition = readPosition(i);
                if (!memoryPosition || !length || !wordPosition)
                {
                    return SyntaxError(std::move(word), commandOffset);
                }
                if (*length == 0) return SyntaxError(std::move(word), commandOffset);
                if (memory.empty() || *memoryPosition > memory.size()
                    || *memoryPosition + *length > memory.size()
                    || *wordPosition > word.size()
                    || word.size() + *length > MAX_PASSWORD_SIZE)
                {
                    return Rejected(std::move(word), commandOffset);
                }
                word.insert(*wordPosition, memory.substr(*memoryPosition, *length));
                break;
            }

            case '4':
                if (memory.empty() || word.size() + memory.size() >= MAX_PASSWORD_SIZE)
                {
                    return Rejected(std::move(word), commandOffset);
                }
                word += memory;
                break;

            case '6':
                if (memory.empty() || word.size() + memory.size() >= MAX_PASSWORD_SIZE)
                {
                    return Rejected(std::move(word), commandOffset);
                }
                word.insert(0, memory);
                break;

            case 'M':
                memory = word;
                break;

            case '<':
            case '>':
            case '_':
            {
                const char operation = commands[i];
                const auto length = readPosition(i);
                if (!length) return SyntaxError(std::move(word), commandOffset);
                const bool reject = (operation == '<' && word.size() > *length)
                    || (operation == '>' && word.size() < *length)
                    || (operation == '_' && word.size() != *length);
                if (reject) return Rejected(std::move(word), commandOffset);
                break;
            }

            case '!':
            case '/':
            {
                const char operation = commands[i];
                const auto value = readCharacter(i);
                if (!value) return SyntaxError(std::move(word), commandOffset);
                const size_t position = word.find(*value);
                if ((operation == '!' && position != std::string::npos)
                    || (operation == '/' && position == std::string::npos))
                {
                    return Rejected(std::move(word), commandOffset);
                }
                if (operation == '/') savedPosition = position;
                break;
            }

            case '(':
            case ')':
            {
                const char operation = commands[i];
                const auto value = readCharacter(i);
                if (!value) return SyntaxError(std::move(word), commandOffset);
                const bool reject = word.empty()
                    || (operation == '(' && word.front() != *value)
                    || (operation == ')' && word.back() != *value);
                if (reject) return Rejected(std::move(word), commandOffset);
                break;
            }

            case '=':
            {
                const auto position = readPosition(i);
                const auto value = readCharacter(i);
                if (!position || !value) return SyntaxError(std::move(word), commandOffset);
                if (*position >= word.size() || word[*position] != *value)
                {
                    return Rejected(std::move(word), commandOffset);
                }
                break;
            }

            case '%':
            {
                const auto count = readPosition(i);
                const auto value = readCharacter(i);
                if (!count || !value) return SyntaxError(std::move(word), commandOffset);
                if (*count > word.size()) return Rejected(std::move(word), commandOffset);
                const auto position = FindOccurrence(word, *count, [value](const char candidate)
                {
                    return candidate == *value;
                });
                if (*count != 0 && !position) return Rejected(std::move(word), commandOffset);
                savedPosition = position;
                break;
            }

            case 'Q':
                if (word == memory) return Rejected(std::move(word), commandOffset);
                break;

            case 'k':
                if (word.size() >= 2) std::swap(word[0], word[1]);
                break;

            case 'K':
                if (word.size() >= 2) std::swap(word[word.size() - 1], word[word.size() - 2]);
                break;

            case '*':
            {
                const auto firstPosition = readPosition(i);
                const auto secondPosition = readPosition(i);
                if (!firstPosition || !secondPosition)
                {
                    return SyntaxError(std::move(word), commandOffset);
                }
                if (*firstPosition < word.size() && *secondPosition < word.size())
                {
                    std::swap(word[*firstPosition], word[*secondPosition]);
                }
                break;
            }

            case 'L':
            case 'R':
            case '+':
            case '-':
            {
                const char operation = commands[i];
                const auto position = readPosition(i);
                if (!position) return SyntaxError(std::move(word), commandOffset);
                if (*position < word.size())
                {
                    auto value = static_cast<unsigned char>(word[*position]);
                    if (operation == 'L') value = static_cast<unsigned char>(value << 1);
                    if (operation == 'R') value = static_cast<unsigned char>(value >> 1);
                    if (operation == '+') value = static_cast<unsigned char>(value + 1);
                    if (operation == '-') value = static_cast<unsigned char>(value - 1);
                    word[*position] = static_cast<char>(value);
                }
                break;
            }

            case '.':
            case ',':
            {
                const char operation = commands[i];
                const auto position = readPosition(i);
                if (!position) return SyntaxError(std::move(word), commandOffset);
                if (operation == '.' && *position + 1 < word.size())
                {
                    word[*position] = word[*position + 1];
                }
                if (operation == ',' && *position > 0 && *position < word.size())
                {
                    word[*position] = word[*position - 1];
                }
                break;
            }

            case 'y':
            case 'Y':
            {
                const char operation = commands[i];
                const auto length = readPosition(i);
                if (!length) return SyntaxError(std::move(word), commandOffset);
                if (*length <= word.size() && word.size() + *length < MAX_PASSWORD_SIZE)
                {
                    if (operation == 'y') word.insert(0, word.substr(0, *length));
                    else word += word.substr(word.size() - *length);
                }
                break;
            }

            case 'E':
                Title(word, ' ');
                break;

            case 'e':
            {
                const auto separator = readCharacter(i);
                if (!separator) return SyntaxError(std::move(word), commandOffset);
                Title(word, *separator);
                break;
            }

            case '3':
            {
                const auto occurrence = readPosition(i);
                const auto separator = readCharacter(i);
                if (!occurrence || !separator) return SyntaxError(std::move(word), commandOffset);
                size_t seen = 0;
                for (size_t position = 0; position < word.size(); ++position)
                {
                    if (word[position] != *separator) continue;
                    if (seen++ != *occurrence) continue;
                    if (position + 1 < word.size()) word[position + 1] = ::Util::ToggleCase(word[position + 1]);
                    break;
                }
                break;
            }

            case '~':
            {
                const auto operation = readCharacter(i);
                if (!operation) return SyntaxError(std::move(word), commandOffset);

                if (*operation == 's')
                {
                    const auto marker = readCharacter(i);
                    const auto characterClass = readCharacter(i);
                    const auto replacement = readCharacter(i);
                    if (!marker || *marker != '?' || !characterClass || !replacement
                        || !IsValidClass(*characterClass))
                    {
                        return SyntaxError(std::move(word), commandOffset);
                    }
                    for (char& value : word)
                    {
                        if (IsClass(value, *characterClass)) value = *replacement;
                    }
                    break;
                }

                if (*operation == '@' || *operation == 'e'
                    || *operation == '!' || *operation == '/'
                    || *operation == '(' || *operation == ')')
                {
                    const auto marker = readCharacter(i);
                    const auto characterClass = readCharacter(i);
                    if (!marker || *marker != '?' || !characterClass || !IsValidClass(*characterClass))
                    {
                        return SyntaxError(std::move(word), commandOffset);
                    }

                    if (*operation == '@')
                    {
                        word.erase(
                            std::remove_if(word.begin(), word.end(), [characterClass](const char value)
                            {
                                return IsClass(value, *characterClass);
                            }),
                            word.end()
                        );
                    }
                    else if (*operation == 'e')
                    {
                        TitleClass(word, *characterClass);
                    }
                    else if (*operation == '!' || *operation == '/')
                    {
                        const auto position = FindClass(word, *characterClass);
                        if ((*operation == '!' && position)
                            || (*operation == '/' && !position))
                        {
                            return Rejected(std::move(word), commandOffset);
                        }
                        if (*operation == '/') savedPosition = position;
                    }
                    else
                    {
                        const bool matches = !word.empty()
                            && IsClass(*operation == '(' ? word.front() : word.back(), *characterClass);
                        if (!matches) return Rejected(std::move(word), commandOffset);
                    }
                    break;
                }

                if (*operation == '=')
                {
                    const auto position = readPosition(i);
                    const auto marker = readCharacter(i);
                    const auto characterClass = readCharacter(i);
                    if (!position || !marker || *marker != '?' || !characterClass
                        || !IsValidClass(*characterClass))
                    {
                        return SyntaxError(std::move(word), commandOffset);
                    }
                    if (*position >= word.size() || !IsClass(word[*position], *characterClass))
                    {
                        return Rejected(std::move(word), commandOffset);
                    }
                    break;
                }

                if (*operation == '%')
                {
                    const auto count = readPosition(i);
                    const auto marker = readCharacter(i);
                    const auto characterClass = readCharacter(i);
                    if (!count || !marker || *marker != '?' || !characterClass
                        || !IsValidClass(*characterClass))
                    {
                        return SyntaxError(std::move(word), commandOffset);
                    }
                    if (*count > word.size()) return Rejected(std::move(word), commandOffset);
                    const auto position = FindOccurrence(word, *count, [characterClass](const char value)
                    {
                        return IsClass(value, *characterClass);
                    });
                    if (*count != 0 && !position) return Rejected(std::move(word), commandOffset);
                    savedPosition = position;
                    break;
                }

                return SyntaxError(std::move(word), commandOffset);
            }

            default:
                return SyntaxError(std::move(word), commandOffset);
        }
    }

    return {Status::Applied, std::move(word), NO_ERROR};
}

}
