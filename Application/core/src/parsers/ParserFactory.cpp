#include "parsers/IDrawingParser.h"
#include "parsers/DxfParser.h"
#include "parsers/PdfParser.h"
#include <algorithm>
#include <cctype>

namespace Application {

    // ─────────────────────────────────────────────
    //  Helpers
    // ─────────────────────────────────────────────
    static std::string get_extension(const std::string& file_path)
    {
        auto pos = file_path.rfind('.');
        if (pos == std::string::npos)
            return "";

        std::string ext = file_path.substr(pos);

        // Lowercase for case-insensitive comparison
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return std::tolower(c); });

        return ext;
    }

    // ─────────────────────────────────────────────
    //  ParserFactory::create
    //  Add new parsers here when new formats are
    //  supported — nowhere else needs to change.
    // ─────────────────────────────────────────────
    std::unique_ptr<IDrawingParser> ParserFactory::create(
        const std::string& file_path)
    {
        std::string ext = get_extension(file_path);

        if (ext == ".dxf" || ext == ".dxb")
            return std::make_unique<DxfParser>();

        if (ext == ".pdf")
            return std::make_unique<PdfParser>();

        // Unsupported format — caller checks for nullptr
        return nullptr;
    }

    // ─────────────────────────────────────────────
    //  ParserFactory::supported_formats
    // ─────────────────────────────────────────────
    std::vector<std::string> ParserFactory::supported_formats()
    {
        return { ".dxf", ".dxb", ".pdf" };
    }

    // ─────────────────────────────────────────────
    //  ParserFactory::is_supported
    // ─────────────────────────────────────────────
    bool ParserFactory::is_supported(const std::string& file_path)
    {
        auto formats = supported_formats();
        std::string ext = get_extension(file_path);
        return std::find(formats.begin(), formats.end(), ext) != formats.end();
    }

} // namespace Application
