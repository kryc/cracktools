//
// RFC 4180 CSV output.
//

#ifndef CsvWriter_hpp
#define CsvWriter_hpp

#include <cstddef>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

class CsvWriter
{
public:
    explicit CsvWriter(std::ostream& Output);

    void WriteRow(const std::span<const std::string> Fields);

private:
    void WriteField(const std::string_view Field);

    std::ostream& m_Output;
};

#endif
