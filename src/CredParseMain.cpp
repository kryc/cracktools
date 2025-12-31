//
//  CredParseMain.cpp
//  CredParse
//
//  Created by Kryc on 29/12/2025.
//  Copyright © 2025 Kryc. All rights reserved.
//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

#include "CrackDatabase.hpp"
#include "LineReader.hpp"
#include "Util.hpp"
#include "UnsafeBuffer.hpp"
#include "WordGenerator.hpp"

#define ARGCHECK() \
    if (argc <= i) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

const bool CouldBeHashHex(
    const std::string_view Value
)
{
    return Util::IsHex(Value) &&
           (Value.size() == 32 || Value.size() == 40 ||
            Value.size() == 64 || Value.size() == 128);
}

// Parse the simple case of colon (or semicolon) separated values
std::optional<std::pair<std::string_view, std::string_view>>
ParseColonSeparated(
    const std::string_view Line
)
{
    const size_t colonPos = Line.find(':');
    const size_t semiPos = Line.find(';');
    std::string_view username, password;

    // Simple case. Only colon and no semicolon and is a valid username/email and the password is non-empty
    if (colonPos != std::string_view::npos && semiPos == std::string_view::npos &&
        Util::IsValidUsernameOrEmail(Line.substr(0, colonPos)) &&
        colonPos + 1 < Line.size())
    {
        username = Line.substr(0, colonPos);
        password = Line.substr(colonPos + 1);
    }
    // Simple case. Only semicolon and no colon and is a valid username/email and the password is non-empty
    else if (semiPos != std::string_view::npos && colonPos == std::string_view::npos &&
             Util::IsValidUsernameOrEmail(Line.substr(0, semiPos)) &&
             semiPos + 1 < Line.size())
    {
        username = Line.substr(0, semiPos);
        password = Line.substr(semiPos + 1);
    }
    // Ambiguous case. Both colon and semicolon present
    else if (colonPos != std::string_view::npos && semiPos != std::string_view::npos)
    {
        // Choose the first separator
        if (colonPos < semiPos && 
            Util::IsValidUsernameOrEmail(Line.substr(0, colonPos)) &&
            colonPos + 1 < Line.size())
        {
            username = Line.substr(0, colonPos);
            password = Line.substr(colonPos + 1);
        }
        else if (semiPos < colonPos &&
                 Util::IsValidUsernameOrEmail(Line.substr(0, semiPos)) &&
                 semiPos + 1 < Line.size())
        {
            username = Line.substr(0, semiPos);
            password = Line.substr(semiPos + 1);
        }
    }
    // If the username of password end in carriage return, strip it
    if (!username.empty() && username.back() == '\r')
    {
        username = username.substr(0, username.size() - 1);
    }
    if (!password.empty() && password.back() == '\r')
    {
        password = password.substr(0, password.size() - 1);
    }
    // Return if both username and password are non-empty
    if (!username.empty() && !password.empty())
    {
        return std::make_pair(username, password);
    }
    return std::nullopt;
}

std::optional<std::pair<std::string_view, std::string_view>>
ParseEmailAndLikelyPassword(
    const std::string_view Line
)
{
    // Look for substrings that look like emails
    std::string_view email, password;
    std::string_view remaining;
    size_t atPos = Line.find('@');

    while (atPos != std::string_view::npos)
    {
        // Find the start of the email
        size_t start = atPos;
        while (start > 0 && EMAIL_LOCAL.find(Line[start - 1]) != std::string_view::npos
                && Line[start - 1] != '\'' && Line[start - 1] != '\"') // Expand it to strip certain quotes
        {
            start--;
        }
        // Find the end of the email
        size_t end = atPos;
        while (end + 1 < Line.size() && EMAIL_DOMAIN.find(Line[end + 1]) != std::string_view::npos
                && Line[end + 1] != '\'' && Line[end + 1] != '\"') // Expand it to strip certain quotes
        {
            end++;
        }
        email = Line.substr(start, end - start + 1);
        if (Util::IsValidEmail(email))
        {
            remaining = Line.substr(end + 1);
            break;
        }
        // Look for the next '@'
        atPos = Line.find('@', atPos + 1);
    }
    // Look for the password in the remaining string
    if (remaining.empty())
    {
        return std::nullopt;
    }
    // Look for likely passwords using characters that are only in COMMON_SHORT
    std::vector<std::string_view> candidates;
    size_t pos = 0;
    while (pos < remaining.size())
    {
        // Skip non-COMMON_SHORT characters
        while (pos < remaining.size() && COMMON_SHORT.find(remaining[pos]) == std::string_view::npos)
        {
            pos++;
        }
        size_t start = pos;
        while (pos < remaining.size() && COMMON_SHORT.find(remaining[pos]) != std::string_view::npos)
        {
            pos++;
        }
        size_t length = pos - start;
        if (length >= 5) // Minimum length for a likely password
        {
            password = remaining.substr(start, length);
            candidates.push_back(password);
        }
    }
    // Now some simple logic to pick the best candidate
    if (candidates.size() == 1)
    {
        return std::make_pair(email, candidates[0]);
    }
    else if (candidates.size() > 1)
    {
        // Default to the first candidate
        std::string_view selected = candidates[0];
        // Choose any hex candidate first
        for (const auto& candidate : candidates)
        {
            if (CouldBeHashHex(candidate))
            {
                return std::make_pair(email, candidate);
            }
        }
        // Ignore numeric-only candidates and dates
        for (const auto& candidate : candidates)
        {
            if (!Util::IsNumericString(candidate) &&
                !Util::IsLikelyDateString(candidate) &&
                !Util::IsValidIPv4(candidate) &&
                candidate != "Banned" &&
                candidate != "default")
            {
                selected = candidate;
            }
        }
        // Strip carriage return if present
        if (!selected.empty() && selected.back() == '\r')
        {
            selected = selected.substr(0, selected.size() - 1);
        }
        return std::make_pair(email, selected);
    }
    return std::nullopt;
}

std::optional<std::pair<std::string_view, std::string_view>>
ParseLikelyUsernameAndPassword(
    const std::string_view Line
)
{
    // Look for likely usernames using characters that are only in COMMON_SHORT
    size_t pos = 0;
    std::string_view username, password;
    while (pos < Line.size())
    {
        // Skip non-COMMON_SHORT characters
        while (pos < Line.size() && COMMON_SHORT.find(Line[pos]) == std::string_view::npos)
        {
            pos++;
        }
        size_t start = pos;
        while (pos < Line.size() && COMMON_SHORT.find(Line[pos]) != std::string_view::npos)
        {
            pos++;
        }
        size_t length = pos - start;
        if (length >= 3) // Minimum length for a likely username
        {
            username = Line.substr(start, length);
            break;
        }
    }
    if (username.empty())
    {
        return std::nullopt;
    }
    // Scan forward for likely passwords
    std::vector<std::string_view> candidates;
    while (pos < Line.size())
    {
        // Skip non-COMMON_SHORT characters
        while (pos < Line.size() && COMMON_SHORT.find(Line[pos]) == std::string_view::npos)
        {
            pos++;
        }
        size_t start = pos;
        while (pos < Line.size() && COMMON_SHORT.find(Line[pos]) != std::string_view::npos)
        {
            pos++;
        }
        size_t length = pos - start;
        if (length >= 5) // Minimum length for a likely password
        {
            password = Line.substr(start, length);
            candidates.push_back(password);
        }
    }
    // Now some simple logic to pick the best candidate
    if (candidates.size() == 1)
    {
        return std::make_pair(username, candidates[0]);
    }
    else if (candidates.size() > 1)
    {
        // Default to the first candidate
        std::string_view selected = candidates[0];
        // Choose any hexadecimal passwords first
        for (const auto& candidate : candidates)
        {
            if (CouldBeHashHex(candidate))
            {
                return std::make_pair(username, candidate);
            }
        }
        // Ignore numeric-only candidates and dates
        for (const auto& candidate : candidates)
        {
            if (!Util::IsNumericString(candidate) &&
                !Util::IsLikelyDateString(candidate) &&
                !Util::IsValidIPv4(candidate) &&
                candidate != "Banned" &&
                candidate != "default")
            {
                selected = candidate;
            }
        }
        // Strip carriage return if present
        if (!selected.empty() && selected.back() == '\r')
        {
            selected = selected.substr(0, selected.size() - 1);
        }
        // If all candidates were numeric or dates, return the first one
        return std::make_pair(username, selected);
    }
    return std::nullopt;
}

std::optional<std::pair<std::string_view, std::string_view>>
ParseCredentials(
    const std::string_view Line
)
{
    auto parsed = ParseColonSeparated(Line);
    if (parsed.has_value())
    {
        return parsed;
    }
    parsed = ParseEmailAndLikelyPassword(Line);
    if (parsed.has_value())
    {
        return parsed;
    }
    parsed = ParseLikelyUsernameAndPassword(Line);
    if (parsed.has_value())
    {
        return parsed;
    }
    return std::nullopt;
}

const bool
Filter(
    const std::string_view Username,
    const std::string_view Password
)
{
    // Custom filtering logic
    if (Username == "email" && Util::IsValidEmail(Password))
    {
        return true;
    }
    else if (Username == "salt" && Password.size() == 8 && Util::IsAlphanumeric(Password, Util::Case::Both))
    {
        return true;
    }
    else if ((Username == "lastip" || Username == "regip") && Util::IsValidIPv4(Password))
    {
        return true;
    }
    else if (Username == "password")
    {
        return true;
    }
    else if (Username == "username")
    {
        return true;
    }
    return false;
}

void
CredParse(
    const std::string_view InputFile,
    const std::string_view OutputFile,
    const std::string_view Database,
    const std::string_view Separator = ":",
    const bool Unique = false,
    const bool Append = false
)
{
    // Check if the input file exists, if not read from stdin
    std::istream* input = &std::cin;
    std::ifstream infile;
    if (!InputFile.empty())
    {
        infile.open(InputFile.data(), std::ios::in | std::ios::binary);
        if (!infile.is_open())
        {
            std::cerr << "Error opening input file: " << InputFile << std::endl;
            return;
        }
        input = &infile;
    }

    // Open the output file if specified, otherwise write to stdout
    std::ostream* output = &std::cout;
    std::ofstream outfile;
    if (!OutputFile.empty())
    {
        std::ios::openmode mode = std::ios::out|std::ios::binary;
        if (Append)
        {
            mode |= std::ios::app;
        }
        outfile.open(OutputFile.data(), mode);
        if (!outfile.is_open())
        {
            std::cerr << "Error opening output file: " << OutputFile << std::endl;
            return;
        }
        output = &outfile;
    }

    CrackDatabase db(Database);

    LineReader<> reader(input);
    std::string_view line;
    std::string temp;

    size_t count = 0;
    size_t success = 0;
    size_t filtered = 0;
    size_t failure = 0;
    size_t unique = 0;
    std::string lastUsername, lastPassword;
    while (reader.ReadLine(line))
    {
        count++;

        auto parsed = ParseCredentials(line);
        if (parsed.has_value())
        {
            if (Filter(parsed->first, parsed->second))
            {
                filtered++;
            }
            else if (Unique && parsed->first == lastUsername && parsed->second == lastPassword)
            {
                unique++;
            }
            else
            {
                success++;
                if (Unique)
                {
                    lastUsername = std::string(parsed->first);
                    lastPassword = std::string(parsed->second);
                }
                if (CouldBeHashHex(parsed->second))
                {
                    auto lookup = db.Lookup(parsed->second);
                    if (lookup.has_value())
                    {
                        // std::cout << "Cracked hash for " << parsed->first << "(" << parsed->second << "): " << lookup.value() << std::endl;
                        *output << parsed->first << Separator << Util::Hexlify(lookup.value()) << std::endl;
                    }
                    else
                    {
                        *output << parsed->first << Separator << Util::Hexlify(parsed->second) << std::endl;
                    }
                }
                else
                {
                    *output << parsed->first << Separator << Util::Hexlify(parsed->second) << std::endl;
                }
            }
        }
        else
        {
            failure++;
        }

        if (count % 1000 == 0 && !OutputFile.empty()) {
            std::cerr << "\r#: " << count << " ✓: " << success << " ✗: " << failure << " F: " << filtered << " U: " << unique << std::flush;
        }
    }
}

int main(
	int argc,
	const char * argv[]
)
{
    auto args = cracktools::ParseArgv(argv, argc);
    std::string_view input_file, output_file;
    std::string_view database;
    std::string_view separator = ":";
    bool unique = false;
    bool append = false;

    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = args[i];
        if (arg == "--output" || arg == "-o")
        {
            ARGCHECK();
            output_file = args[++i];
        }
        else if (arg == "--append" || arg == "-a")
        {
            append = true;
        }
        else if (input_file.empty() && std::filesystem::exists(arg))
        {
            input_file = arg;
        }
        else if (arg == "--separator" || arg == "-s")
        {
            ARGCHECK();
            separator = args[++i];
        }
        else if (arg == "--database" || arg == "-d")
        {
            ARGCHECK();
            database = args[++i];
        }
        else if (arg == "--unique" || arg == "-u")
        {
            unique = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << args[0] << " [options] [input_file]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --output, -o <file>  Specify the output file" << std::endl;
            std::cout << "  --append, -a         Append to the output file if it exists" << std::endl;
            std::cout << "  --unique, -u         Output only unique username:password pairs" << std::endl;
            std::cout << "  --separator, -s <sep> Specify the separator (default is ':')" << std::endl;
            std::cout << "  --database, -d <db>  Specify the database directory" << std::endl;
            std::cout << "  --help, -h           Show this help message" << std::endl;
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    CredParse(input_file, output_file, database, separator, unique, append);
}