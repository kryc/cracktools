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
#include <tuple>

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

std::vector<std::string_view>
ParsePossibleUsersAndPasswords(
    const std::string_view Line,
    const size_t MinLength = 5,
    const size_t MaxLength = 31
)
{
    static const std::string_view IGNORE_CHARS = " \t,|'\":;";
    std::vector<std::string_view> results;
    // Parse through the line looking for strings of characters separated by ignore chars
    size_t pos = 0;
    while (pos < Line.size())
    {
        // Skip ignore chars
        while (pos < Line.size() && IGNORE_CHARS.find(Line[pos]) != std::string_view::npos)
        {
            pos++;
        }
        size_t start = pos;
        while (pos < Line.size() && IGNORE_CHARS.find(Line[pos]) == std::string_view::npos)
        {
            pos++;
        }
        auto word = Line.substr(start, pos - start);
        if (word.size() >= MinLength && word.size() <= MaxLength)
        {
            results.push_back(word);
        }
    }
    return results;
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
ParseLikelyUsernameAndPassword(
    const std::string_view Line,
    const size_t MinLength,
    const size_t MaxLength
)
{
    std::string_view username, password;

    // Parse possible usernames and passwords
    auto possible = ParsePossibleUsersAndPasswords(Line, MinLength, MaxLength);
    if (possible.size() < 2)
    {
        return std::nullopt;
    }

    // Remove elements that are clearly auxiliary data
    for (auto it = possible.begin(); it != possible.end(); )
    {
        if (Util::IsLikelyDateString(*it) ||
            Util::IsValidIPv4(*it))
        {
            it = possible.erase(it);
        }
        else
        {
            ++it;
        }
    }
    // Need at least two candidates remaining
    if (possible.size() < 2)
    {
        return std::nullopt;
    }
    
    // First see if any are email addresses, this is likely the username
    for (const auto& candidate : possible)
    {
        if (Util::IsValidEmail(candidate))
        {
            username = candidate;
            break;
        }
    }

    // Next see if any are valid hashes, this will likely be the password
    for (const auto& candidate : possible)
    {
        if (Util::CouldBeHashHex(candidate) || Util::CouldBeCryptHash(candidate))
        {
            password = candidate;
            break;
        }
    }

    // Next see if there are any hashes as substrings of candidates
    // Scan through looking for strings of hex characters, then check the lengths
    if (password.empty())
    {
        for (const auto& candidate : possible)
        {
            size_t start = 0;
            while (start < candidate.size())
            {
                // Find the start of a hex substring
                while (start < candidate.size() && !Util::IsHex(candidate[start]))
                {
                    start++;
                }
                size_t end = start;
                while (end < candidate.size() && Util::IsHex(candidate[end]))
                {
                    end++;
                }
                size_t length = end - start;
                if (length == 32 || length == 40 || length == 64 || length == 128)
                {
                    password = candidate.substr(start, length);
                    break;
                }
                start = end;
            }
            if (!password.empty())
            {
                break;
            }
        }
    }

    // Simple case logic in instances where we have only two candidates
    if (possible.size() == 2)
    {
        if (username.empty() && !Util::IsValidEmail(possible[0]))
        {
            username = possible[0];
        }
        if (password.empty() && possible[1] != username)
        {
            password = possible[1];
        }
    }

    if (!username.empty() && !password.empty())
    {
        return std::make_pair(username, password);
    }

    // We are approaching guesswork now
    // If there are two elements, just return them
    if (possible.size() == 2)
    {
        return std::make_pair(possible[0], possible[1]);
    }

    // If there are more, pick use some simple filtering to choose two
    for (const auto& candidate : possible)
    {
        if (username.empty() && Util::IsValidUsername(candidate))
        {
            username = candidate;
        }
        else if (!username.empty() && password.empty() &&
                 candidate != "Banned" &&
                 candidate != "default")
        {
            password = candidate;
        }
    }

    if (!username.empty() && !password.empty())
    {
        return std::make_pair(username, password);
    }

    return std::nullopt;
}

std::optional<std::tuple<std::string_view, std::string_view, int>>
ParseCredentials(
    const std::string_view Line,
    const size_t Min,
    const size_t Max
)
{
    auto parsed = ParseColonSeparated(Line);
    if (parsed.has_value())
    {
        if (parsed->first.size() < Min || parsed->first.size() > Max ||
            parsed->second.size() < Min || parsed->second.size() > Max)
        {
            return std::nullopt;
        }
        return std::make_tuple(parsed->first, parsed->second, 1);
    }
    parsed = ParseLikelyUsernameAndPassword(Line, Min, Max);
    if (parsed.has_value())
    {
        return std::make_tuple(parsed->first, parsed->second, 2);
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
    if ((Username == "email" || Username == "[Email]") && Util::IsValidEmail(Password))
    {
        return true;
    }
    else if (Username == "telephone" || Username == "[Telephone]" || Username == "phone" || Username == "[Phone]")
    {
        return true;
    }
    else if (Username == "address" || Username == "[Address]")
    {
        return true;
    }
    else if (Username == "name" || Username == "[Name]")
    {
        return true;
    }
    else if (Username == "website" || Username == "[Website]")
    {
        return true;
    }
    else if (Username == "comment" || Username == "[Comment]")
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
    const size_t Min,
    const size_t Max,
    const std::string_view Database,
    const std::string_view Separator = ":",
    const bool Unique = false,
    const bool Append = false,
    const bool OutputMatchRule = false
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
    size_t cracked = 0;
    size_t failed_to_crack = 0;
    std::string lastUsername, lastPassword;
    while (reader.ReadLine(line))
    {
        count++;
        // Remove trailing carriage return
        if (line.size() > 0 && line.back() == '\r')
        {
            line = line.substr(0, line.size() - 1);
        }

        // Ignore any lines that are not valid printable UTF8
        if (!Util::IsPrintableUTF8(line))
        {
            failure++;
            continue;
        }

        // Remove leading whitespace
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
        {
            start++;
        }
        line = line.substr(start);

        auto parsed = ParseCredentials(line, Min, Max);
        if (parsed.has_value())
        {
            if (Filter(std::get<0>(parsed.value()), std::get<1>(parsed.value())))
            {
                filtered++;
            }
            else if (Unique && std::get<0>(parsed.value()) == lastUsername && std::get<1>(parsed.value()) == lastPassword)
            {
                unique++;
            }
            else
            {
                success++;
                if (Unique)
                {
                    lastUsername = std::string(std::get<0>(parsed.value()));
                    lastPassword = std::string(std::get<1>(parsed.value()));
                }
                if (OutputMatchRule)
                {
                    const int matchRule = std::get<2>(parsed.value());
                    *output << std::to_string(matchRule) + std::string(Separator);
                }
                const std::string_view foundUser = std::get<0>(parsed.value());
                const std::string_view foundPass = std::get<1>(parsed.value());
                std::string_view outPass = foundPass;
                std::string tempPass;

                if (Util::CouldBeHashHex(foundPass))
                {
                    auto lookup = db.Lookup(foundPass);
                    if (lookup.has_value())
                    {
                        // std::cout << "Cracked hash for " << foundUser << "(" << foundPass << "): " << lookup.value() << std::endl;
                        cracked++;
                        tempPass = lookup.value();
                        outPass = tempPass;
                    }
                    else
                    {
                        failed_to_crack++;
                    }
                }

                if (!Util::IsHexlified(foundPass) && Util::NeedsHexlify(foundPass))
                {
                    tempPass = Util::Hexlify(foundPass);
                    outPass = tempPass;
                }

                *output << foundUser << Separator << outPass << std::endl;
            }
        }
        else
        {
            failure++;
        }

        if (count % 1000 == 0 && !OutputFile.empty()) {
            std::cerr << "\r#: " << count << " ✓: " << success << " ✗: " << failure << " F: " << filtered << " U: " << unique << " C: " << cracked << "/" << failed_to_crack + cracked << std::flush;
        }
    }

    if (!OutputFile.empty()) {
        std::cerr << "\r#: " << count << " ✓: " << success << " ✗: " << failure << " F: " << filtered << " U: " << unique << " C: " << cracked << "/" << failed_to_crack + cracked << std::endl;
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
    bool output_match_rule = false;
    size_t min = 5;
    size_t max = std::numeric_limits<size_t>::max();

    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = args[i];
        if (arg == "--output" || arg == "-o")
        {
            ARGCHECK();
            output_file = args[++i];
        }
        else if (arg == "--min" || arg == "-m")
        {
            ARGCHECK();
            min = std::stoull(std::string(args[++i]));
        }
        else if (arg == "--max" || arg == "-M")
        {
            ARGCHECK();
            max = std::stoull(std::string(args[++i]));
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
        else if (arg == "--output-match-rule" || arg == "-r")
        {
            output_match_rule = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << args[0] << " [options] [input_file]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --output, -o <file>  Specify the output file" << std::endl;
            std::cout << "  --min, -m <number>   Specify the minimum number of lines to process" << std::endl;
            std::cout << "  --max, -M <number>   Specify the maximum number of lines to process" << std::endl;
            std::cout << "  --append, -a         Append to the output file if it exists" << std::endl;
            std::cout << "  --unique, -u         Output only unique username:password pairs" << std::endl;
            std::cout << "  --separator, -s <sep> Specify the separator (default is ':')" << std::endl;
            std::cout << "  --database, -d <db>  Specify the database directory" << std::endl;
            std::cout << "  --output-match-rule, -r  Output the match rule used for cracking" << std::endl;
            std::cout << "  --help, -h           Show this help message" << std::endl;
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    CredParse(input_file, output_file, min, max, database, separator, unique, append, output_match_rule);
}