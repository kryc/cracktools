//
// RFC 4180 CSV output.
//

#include "CsvWriter.hpp"

CsvWriter::CsvWriter(
    std::ostream& Output
) :
    m_Output(Output)
{
}

void
CsvWriter::WriteField(
    const std::string_view Field
)
{
    const bool RequiresQuotes = Field.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!RequiresQuotes)
    {
        m_Output.write(Field.data(), static_cast<std::streamsize>(Field.size()));
        return;
    }

    m_Output.put('"');
    for (const char Character : Field)
    {
        if (Character == '"') m_Output.put('"');
        m_Output.put(Character);
    }
    m_Output.put('"');
}

void
CsvWriter::WriteRow(
    const std::span<const std::string> Fields
)
{
    for (size_t i = 0; i < Fields.size(); i++)
    {
        if (i != 0) m_Output.put(',');
        WriteField(Fields[i]);
    }
    m_Output << "\r\n";
}
