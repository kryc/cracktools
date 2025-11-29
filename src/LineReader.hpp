//
//  LineReader.hpp
//  Cracktools
//
//  Created by Kryc on 29/11/2025.
//  Copyright © 5 Kryc. All rights reserved.
//

#include <array>
#include <fstream>
#include <iostream>
#include <string_view>
#include <optional>
#include <vector>

#include "UnsafeBuffer.hpp"

#ifndef LineReader_hpp
#define LineReader_hpp

template <size_t BlockSize = 16384>
class LineReader {
public:
    LineReader(std::istream* FileStream) : m_File(FileStream) {
        if (!*m_File) {
            throw std::runtime_error("File is not open");
        }
        m_BufferView = cracktools::AsStringView(m_Buffer);
    }
    LineReader(const std::string_view filename) {
        m_FileStream.open(filename.data(), std::ios::in | std::ios::binary);
        if (!m_FileStream.is_open()) {
            throw std::runtime_error("File is not open");
        }
        m_File = &m_FileStream;
        m_BufferView = cracktools::AsStringView(m_Buffer);
    }
    ~LineReader() {
        if (m_FileStream.is_open()) {
            m_FileStream.close();
        }
    }
    const size_t getBlockSize() const {
        return BlockSize;
    }
    std::optional<std::string_view> readLine() {
        if (m_Pending.empty() && !m_File->eof()) {
            m_File->read(m_Buffer.data(), BlockSize);
            const size_t bytesRead = m_File->gcount();
            if (bytesRead == 0) {
                return std::nullopt;
            }
            m_Pending = m_BufferView.substr(0, bytesRead);
        }

        if (m_Pending.empty()) {
            return std::nullopt;
        }
        
        size_t lineEnd = m_Pending.find('\n');
        if (lineEnd == std::string_view::npos) {
            if (m_File->eof() && m_Pending.size() > 0) {
                // At EOF: the remainder is the last line (which
                // may not be terminated by a newline character).
                std::string_view line = m_Pending;
                m_Pending = std::string_view();
                return line;
            } else if (m_File->eof()) {
                // At EOF: no more lines to read.
                return std::nullopt;
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
                return m_TempLine;
            } else {
                // Not at EOF yet. Move the remaining bytes to the
                // beginning of the buffer and read more data.
                std::copy(m_Pending.begin(), m_Pending.end(), m_Buffer.begin());
                m_File->read(m_Buffer.data() + m_Pending.size(), BlockSize - m_Pending.size());
                const size_t bytesRead = m_File->gcount();
                // Update pending
                m_Pending = m_BufferView.substr(0, m_Pending.size() + bytesRead);
                return readLine();
            }
        }

        auto line = m_Pending.substr(0, lineEnd);
        m_Pending = m_Pending.substr(lineEnd + 1);
        return line;
    }
private:
    std::istream* m_File = nullptr;
    std::ifstream m_FileStream;
    std::array<char, BlockSize> m_Buffer;
    std::string_view m_BufferView;
    std::string_view m_Pending;
    std::string m_TempLine;
};
#endif