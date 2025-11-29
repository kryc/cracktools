//
//  LineReader.hpp
//  Cracktools
//
//  Created by Kryc on 29/11/2025.
//  Copyright © 5 Kryc. All rights reserved.
//

#include <array>
#include <iostream>
#include <fstream>
#include <string_view>
#include <optional>
#include <vector>

#include "UnsafeBuffer.hpp"

#ifndef LineReader_hpp
#define LineReader_hpp

template <size_t BlockSize = 8192>
class LineReader {
public:
    LineReader(const std::string_view filename) {
        m_File.open(filename.data(), std::ios::in | std::ios::binary);
        if (!m_File.is_open()) {
            throw std::runtime_error("Could not open file");
        }
        m_BufferView = cracktools::AsStringView(m_Buffer);
    }
    ~LineReader() {
        if (m_File.is_open()) {
            m_File.close();
        }
    }
    const size_t getBlockSize() const {
        return BlockSize;
    }
    std::optional<std::string_view> readLine() {
        if (m_Pending.empty() && !m_File.eof()) {
            m_File.read(m_Buffer.data(), BlockSize);
            const size_t bytesRead = m_File.gcount();
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
            if (m_File.eof() && m_Pending.size() > 0) {
                // At EOF: the remainder is the last line (which
                // may not be terminated by a newline character).
                std::string_view line = m_Pending;
                m_Pending = std::string_view();
                return line;
            } else if (m_File.eof()) {
                // At EOF: no more lines to read.
                return std::nullopt;
            } else if (m_Pending.size() == BlockSize) {
                // The next line is longer than the buffer size, so
                // we cannot read it. Skip this line and continue.
                m_Pending = std::string_view();
                // Seek forward to the next newline character.
                while (!m_File.eof()) {
                    char c;
                    m_File.get(c);
                    if (c == '\n') {
                        break;
                    }
                }
                return readLine();
            } else {
                // Not at EOF yet: treat as partial line and rewind
                // so the next read starts from the beginning of it.
                std::streamoff rewind = -static_cast<std::streamoff>(m_Pending.size());
                m_File.seekg(rewind, std::ios::cur);
                m_Pending = std::string_view();
                return readLine();
            }
        }

        auto line = m_Pending.substr(0, lineEnd);
        m_Pending = m_Pending.substr(lineEnd + 1);
        return line;
    }
private:
    std::ifstream m_File;
    std::array<char, BlockSize> m_Buffer;
    std::string_view m_BufferView;
    std::string_view m_Pending;
};
#endif