//
//  HashPipeMain.cpp
//  HashPipe
//
//  Created by Kryc on 28/08/2025.
//  Copyright © 2025 Kryc. All rights reserved.
//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "SimdHash.hpp"
#include "Util.hpp"

#include "UnsafeBuffer.hpp"

#define ARGCHECK() \
    if (argc <= i) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

enum class OutputCase
{
    Lower,
    Upper,
    Both
};

void HashPipe(
    const std::string_view Input,
    const std::string_view Output,
    const std::span<const HashAlgorithm> Algorithms,
    const OutputCase Case
)
{
    // Check if the input file exists, if not read from stdin
    std::istream* input = &std::cin;
    std::ifstream infile;
    if (!Input.empty())
    {
        infile.open(Input.data(), std::ios::in | std::ios::binary);
        if (!infile.is_open())
        {
            std::cerr << "Error opening input file: " << Input << std::endl;
            return;
        }
        input = &infile;
    }

    // Open the output file if specified, otherwise write to stdout
    std::ostream* output = &std::cout;
    std::ofstream outfile;
    if (!Output.empty())
    {
        outfile.open(Output.data(), std::ios::out | std::ios::binary);
        if (!outfile.is_open())
        {
            std::cerr << "Error opening output file: " << Output << std::endl;
            return;
        }
        output = &outfile;
    }

    // Read the input line by line, hash each line with each algorithm, and write the results to the output
    std::string line;
    std::array<uint8_t, MAX_HASH_SIZE> hash_output;
    while (std::getline(*input, line))
    {
        // Handle parsing "$HEX[]" input.
        line = Util::UnHexlify(line);
        
        for (const auto& algo : Algorithms)
        {
            // Do the hash
            SimdHashSingle(
                algo,
                line.size(),
                (const uint8_t*)line.data(),
                hash_output.data()
            );

            // Get the hash length in bytes
            const size_t hash_length = GetHashWidth(algo);
            // Get the subspan for the algorithm
            std::span<const uint8_t> hash_span(hash_output);
            hash_span = hash_span.subspan(0, hash_length);
            // Convert to hex
            std::string hash_hex = Util::ToHex(hash_span);
            if (Case != OutputCase::Upper)
            {
                std::transform(hash_hex.begin(), hash_hex.end(), hash_hex.begin(), ::tolower);
                *output << hash_hex << std::endl;
            }
            if (Case != OutputCase::Lower)
            {
                std::transform(hash_hex.begin(), hash_hex.end(), hash_hex.begin(), ::toupper);
                *output << hash_hex << std::endl;
            }
            
        }
    }
}

int main(
	int argc,
	const char * argv[]
)
{
    std::vector<HashAlgorithm> algorithms;

    auto args = cracktools::ParseArgv(argv, argc);
    std::string_view input_file, output_file;
    OutputCase output_case = OutputCase::Lower;

    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = args[i];
        if (arg == "--md4")
        {
            algorithms.push_back(HashAlgorithmMD4);
        }
        else if (arg == "--md5")
        {
            algorithms.push_back(HashAlgorithmMD5);
        }
        else if (arg == "--sha1")
        {
            algorithms.push_back(HashAlgorithmSHA1);
        }
        else if (arg == "--sha256")
        {
            algorithms.push_back(HashAlgorithmSHA256);
        }
        else if (arg == "--sha384")
        {
            algorithms.push_back(HashAlgorithmSHA384);
        }
        else if (arg == "--sha512")
        {
            algorithms.push_back(HashAlgorithmSHA512);
        }
        else if (arg == "--ntlm")
        {
            algorithms.push_back(HashAlgorithmNTLM);
        }
        else if (arg == "--case")
        {
            ARGCHECK();
            const std::string_view case_arg = args[++i];
            if (case_arg == "lower")
            {
                output_case = OutputCase::Lower;
            }
            else if (case_arg == "upper")
            {
                output_case = OutputCase::Upper;
            }
            else if (case_arg == "both")
            {
                output_case = OutputCase::Both;
            }
            else
            {
                std::cerr << "Invalid case option: " << case_arg << std::endl;
                return 1;
            }
        }
        else if (arg == "--input" || arg == "-i")
        {
            ARGCHECK();
            input_file = args[++i];
        }
        else if (arg == "--output" || arg == "-o")
        {
            ARGCHECK();
            output_file = args[++i];
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "HashPipe - A simple hash processing tool\n\n"
                      << "Usage: HashPipe [options]\n\n"
                      << "Options:\n"
                      << "  --input, -i <file>       Input file (default: stdin)\n"
                      << "  --output, -o <file>      Output file (default: stdout)\n"
                      << "  --md4                    Include MD4 algorithm\n"
                      << "  --md5                    Include MD5 algorithm\n"
                      << "  --sha1                   Include SHA1 algorithm\n"
                      << "  --sha256                 Include SHA256 algorithm\n"
                      << "  --sha384                 Include SHA384 algorithm\n"
                      << "  --sha512                 Include SHA512 algorithm\n"
                      << "  --ntlm                   Include NTLM algorithm\n"
                      << "  --case <lower|upper|both> Output case format (default: lower)\n"
                      << "  --help, -h               Show this help message\n";
            return 0;
        }
        else if (arg.starts_with("--"))
        {
            std::cerr << "Unknown option " << arg << std::endl;
            return 1;
        }
    }

    // If no algorithms specified add all of them to the list
    if (algorithms.empty())
    {
        algorithms.insert(algorithms.end(), simdhash::SimdHashAlgorithms.begin(), simdhash::SimdHashAlgorithms.end());
    }

    HashPipe(input_file, output_file, algorithms, output_case);

}