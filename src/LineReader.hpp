//
//  LineReader.hpp
//  Cracktools
//
//  Created by Kryc on 29/11/2025.
//  Copyright © 5 Kryc. All rights reserved.
//

#include <fstream>
#include <iostream>
#include <string_view>
#include <optional>
#include <vector>

#if (defined(__AVX512F__) || defined(__AVX2__))
#include <immintrin.h>
#endif

#include "UnsafeBuffer.hpp"

#ifndef LineReader_hpp
#define LineReader_hpp

template <size_t BlockSize = 16384*2>
class LineReader {
public:
    LineReader(std::istream* FileStream) : m_File(FileStream) {
        m_Buffer.resize(BlockSize);
        m_BufferView = cracktools::AsStringView(m_Buffer);
    }
    LineReader(const std::string_view Filename) {
        SetInputFile(Filename);
        m_Buffer.resize(BlockSize);
        m_BufferView = cracktools::AsStringView(m_Buffer);
    }
    LineReader(void) {
        m_Buffer.resize(BlockSize);
        m_BufferView = cracktools::AsStringView(m_Buffer);
    }
    ~LineReader() {
        if (m_FileStream.is_open()) {
            m_FileStream.close();
        }
    }
    const bool Open(void) {
        return m_Opened;
    }
    bool SetInputFile(const std::string_view Filename) {
        if (m_FileStream.is_open()) {
            m_FileStream.close();
        }
        m_Opened = false;
        m_FileStream.open(Filename.data(), std::ios::in | std::ios::binary);
        if (!m_FileStream.is_open()) {
            return false;
        }
        m_File = &m_FileStream;
        m_BufferView = cracktools::AsStringView(m_Buffer);
        m_Opened = true;
        return true;
    }
    bool SetFileStream(std::istream* FileStream) {
        if (FileStream == nullptr) {
            return false;
        }
        m_Opened = false;
        if (m_FileStream.is_open()) {
            m_FileStream.close();
        }
        m_File = FileStream;
        if (!*m_File) {
            return false;
        }
        m_Opened = true;
        return true;
    }
    const size_t GetBlockSize() const {
        return BlockSize;
    }
    const bool IsEof() const {
        return m_File->eof() && m_Pending.empty();
    }
    const bool ReadLine(std::string_view& Destination) {
        if (m_Pending.empty() && !m_File->eof()) {
            m_File->read(m_Buffer.data(), BlockSize);
            const size_t bytesRead = m_File->gcount();
            if (bytesRead == 0) {
                return false;
            }
            m_Pending = m_BufferView.substr(0, bytesRead);
        }

        if (m_Pending.empty()) {
            return false;
        }
        
        size_t lineEnd = m_Pending.find('\n');
        if (lineEnd == std::string_view::npos) {
            if (m_File->eof() && m_Pending.size() > 0) {
                // At EOF: the remainder is the last line (which
                // may not be terminated by a newline character).
                std::string_view line = m_Pending;
                m_Pending = std::string_view();
                Destination = line;
                return true;
            } else if (m_File->eof()) {
                // At EOF: no more lines to read.
                return false;
            } else if (m_Pending.size() == BlockSize) {
                // The next line is longer than the buffer size, so
                // we cannot read it within the buffer. Read the line into a
                // temporary string and return it.
                m_TempLine = std::string(m_Pending);
                std::string remaining;
                std::getline(*m_File, remaining);
                m_TempLine += remaining;
                // Clear pending so we read another block of data.
                m_Pending = std::string_view();
                Destination = m_TempLine;
                return true;
            } else {
                // Not at EOF yet. Move the remaining bytes to the
                // beginning of the buffer and read more data.
                std::copy(m_Pending.begin(), m_Pending.end(), m_Buffer.begin());
                const size_t bytesRead = cracktools::ReadStream(m_File, m_Buffer, BlockSize - m_Pending.size(), m_Pending.size());
                // Update pending
                m_Pending = m_BufferView.substr(0, m_Pending.size() + bytesRead);
                return ReadLine(Destination);
            }
        }

        auto line = m_Pending.substr(0, lineEnd);
        m_Pending = m_Pending.substr(lineEnd + 1);
        Destination = line;
        return true;
    }
    std::optional<std::string_view> ReadLine() {
        std::string_view line;
        if (ReadLine(line)) {
            return line;
        }
        return std::nullopt;
    }
private:
    std::istream* m_File = nullptr;
    std::ifstream m_FileStream;
    std::vector<char> m_Buffer;
    std::string_view m_BufferView;
    std::string_view m_Pending;
    std::string m_TempLine;
    bool m_Opened = false;
};

template <size_t BlockSize = 16384*2>
class LineCounter {
public:
    LineCounter(std::string_view Filename) {
        m_FileStream.open(Filename.data(), std::ios::in |  std::ios::binary);
        m_Buffer.resize(BlockSize);
        m_BufferView = cracktools::AsStringView(m_Buffer);
    }
    ~LineCounter() {
        if (m_FileStream.is_open()) {
            m_FileStream.close();
        }
    }

#if defined(__AVX512F__) || defined(__AVX2__)
    static_assert(BlockSize % 32 == 0, "BlockSize must be a multiple of 32 for AVX2");
    const size_t CountLines(void) {
        static const __m256i newline = _mm256_set1_epi8('\n');
        auto bufferSpan = cracktools::SpanCast<__m256i>(m_BufferView);
        size_t lineCount = 0;
        while (!m_FileStream.eof()) {
            m_FileStream.read(m_Buffer.data(), BlockSize);
            const size_t bytesRead = m_FileStream.gcount();
            if (bytesRead == 0) {
                break;
            }
            // Fill the remainder of the buffer with zeros if needed
            if (bytesRead < BlockSize) {
                std::fill(m_Buffer.begin() + bytesRead, m_Buffer.end(), 0);
            }
            for (auto chunk : bufferSpan) {
                // Compare each byte in the chunk to '\n'
                __m256i cmp = _mm256_cmpeq_epi8(chunk, newline);
                // Create a bitmask from the comparison result
                uint32_t mask = _mm256_movemask_epi8(cmp);
                // Count the number of set bits in the mask
                lineCount += __builtin_popcount(mask);
            }
        }
        return lineCount;
    }
#elif defined(__arm64__) || defined(__aarch64__)
    static_assert(BlockSize % 16 == 0, "BlockSize must be a multiple of 16 for NEON");
    const size_t CountLines(void) {
        static const uint8x16_t newline = vdupq_n_u8('\n');
        auto bufferSpan = cracktools::SpanCast<uint8x16_t>(m_BufferView);
        size_t lineCount = 0;
        while (!m_FileStream.eof()) {
            m_FileStream.read(m_Buffer.data(), BlockSize);
            const size_t bytesRead = m_FileStream.gcount();
            if (bytesRead == 0) {
                break;
            }
            // Fill the remainder of the buffer with zeros if needed
            if (bytesRead < BlockSize) {
                std::fill(m_Buffer.begin() + bytesRead, m_Buffer.end(), 0);
            }
            for (auto chunk : bufferSpan) {
                // Compare each byte in the chunk to '\n'
                uint8x16_t cmp = vceqq_u8(chunk, newline);
                // Create a bitmask from the comparison result
                uint64_t mask = vmovq_u64(vreinterpretq_u64_u8(cmp));
                // Count the number of set bits in the mask
                lineCount += __builtin_popcountll(mask);
            }
        }
        return lineCount;
    }
#else
    const size_t CountLines(void) {
        size_t lineCount = 0;
        while (!m_FileStream.eof()) {
            m_FileStream.read(m_Buffer.data(), BlockSize);
            const size_t bytesRead = m_FileStream.gcount();
            if (bytesRead == 0) {
                break;
            }
            auto bufferView = m_BufferView.substr(0, bytesRead);
            lineCount += std::count(bufferView.begin(), bufferView.end(), '\n');
        }
        return lineCount;
    }
#endif
private:
    std::ifstream m_FileStream;
    std::vector<char> m_Buffer;
    std::string_view m_BufferView;
};

#endif