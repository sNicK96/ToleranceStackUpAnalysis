#pragma once

#include "models/Dimension.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace Application {

// -------------------------------------------------------------------
//  ParseError
//  Structured error returned when parsing fails.
//  Returned by value instead of throwing exceptions
// -------------------------------------------------------------------
struct ParseError
{
    enum class Code
    {
        FileNotFound,           // file path does not exist
        UnsupportedFormat,      // extension or header not recognised
        CorruptedFile,          // file exists but cannot be read correctly
        EmptyFile,              // file has no content
        NoDimensionsFound,      // parsed successfully but zero dimensions extracted
        PartialParse            // some entities parsed, some failed — data may be incomplete
    };

    Code        code;
    std::string message;        // human-readable description of the error
    std::string source_file;    // which file caused the error
    int         line = -1;      // line / entity index where error occurred (-1 if unknown)

    // Convenience factory methods
    static ParseError file_not_found(const std::string& path)
    {
        return { Code::FileNotFound,
                 "File not found: " + path,
                 path, -1 };
    }

    static ParseError unsupported_format(const std::string& path)
    {
        return { Code::UnsupportedFormat,
                 "Unsupported file format: " + path,
                 path, -1 };
    }

    static ParseError corrupted(const std::string& path, int line = -1)
    {
        return { Code::CorruptedFile,
                 "File appears corrupted or malformed: " + path,
                 path, line };
    }

    static ParseError no_dimensions(const std::string& path)
    {
        return { Code::NoDimensionsFound,
                 "No dimensions found in file: " + path,
                 path, -1 };
    }

    std::string to_string() const
    {
        std::string out = "[ParseError] " + message;
        if (line >= 0)
            out += " (at line " + std::to_string(line) + ")";
        return out;
    }
};

// -------------------------------------------------------------------
//  ParseResult
//  Discriminated union - either a DimensionSet
//  OR a ParseError. Never both, never neither.
// -------------------------------------------------------------------
struct ParseResult
{
    bool                        success = false;
    std::optional<DimensionSet> data;       // populated when success = true
    std::optional<ParseError>   error;      // populated when success = false

    // Named constructors

    static ParseResult ok(DimensionSet dims)
    {
        ParseResult r;
        r.success = true;
        r.data    = std::move(dims);
        return r;
    }

    static ParseResult fail(ParseError err)
    {
        ParseResult r;
        r.success = false;
        r.error   = std::move(err);
        return r;
    }

    // Convenience checks

    bool has_data()  const { return success && data.has_value(); }
    bool has_error() const { return !success && error.has_value(); }

    // Dimension count - 0 if parse failed
    std::size_t dimension_count() const
    {
        return has_data() ? data->count() : 0;
    }
};

// -------------------------------------------------------------------
//  ParseOptions
//  Configuration passed to the parser.
//  Controls what gets extracted and how.
// -------------------------------------------------------------------
struct ParseOptions
{
    // What to extract
    bool    extract_linear_dims     = true;     // linear length dimensions
    bool    extract_diameter_dims   = true;     // diameter / radius dimensions
    bool    extract_angular_dims    = true;     // angular dimensions
    bool    extract_gdt             = true;     // GD&T tolerance frames
    bool    extract_surface_finish  = false;    // Ra / Rz surface finish callouts

    // Layer filtering
    // If non-empty, only entities on these layers are parsed.
    // If empty, all layers are parsed.
    // Example: { "DIMENSIONS", "GDT", "ANNOTATIONS" }
    std::vector<std::string> layer_filter;

    // Tolerance behaviour
    bool    expect_metric           = true;     // true = mm, false = inches
    double  general_tolerance       = 0.1;      // fallback tolerance when none specified
                                                // e.g. title block says "±0.1 unless noted"

    // DXF specific
    bool    parse_blocks            = true;     // follow BLOCK references (assemblies)
    bool    parse_xrefs             = false;    // follow external references (xrefs)
                                                // false by default — xrefs need file access

    // PDF specific
    int     target_page             = -1;       // -1 = all pages, 0-based index otherwise
    double  min_text_height         = 1.5;      // ignore text smaller than this (noise filter)
};

// -------------------------------------------------------------------
//  IDrawingParser
//  Abstract base class - all format parsers
//  must implement this interface.
//
//  Implement for each format:
//    - DxfParser  : handles .dxf files
//    - PdfParser  : handles .pdf files
//    - (future)     StepParser, IgesParser ...
// -------------------------------------------------------------------
class IDrawingParser
{
public:
    virtual ~IDrawingParser() = default;

    // Core interface (must implement)

    // Parse a drawing file and return all extracted dimensions.
    // This is the main entry point — called by the engine.
    virtual ParseResult parse(
        const std::string&  file_path,
        const ParseOptions& options = ParseOptions{}) = 0;

    // Returns true if this parser can handle the given file.
    // Checks extension first, then optionally inspects file header bytes.
    virtual bool can_parse(const std::string& file_path) const = 0;

    // Human-readable parser name — used in logs and UI diagnostics.
    // e.g. "DXF Parser v1.0"
    virtual std::string name() const = 0;

    // Returns supported file extensions.
    // e.g. { ".dxf", ".dxb" } for DxfParser
    virtual std::vector<std::string> supported_extensions() const = 0;

    // Optional overrides

    // Validate file without full parse — fast pre-check before committing.
    // Default just calls can_parse(). Override for deeper validation
    // e.g. DxfParser can verify "AC10" header magic bytes.
    virtual bool validate(const std::string& file_path) const
    {
        return can_parse(file_path);
    }

    // Returns the parser version string — useful for diagnostics.
    // Default returns "1.0.0".
    virtual std::string version() const { return "1.0.0"; }

    // Non-copyable
    // Parsers hold internal state during parsing.
    // Copying mid-parse would be undefined behaviour.
    IDrawingParser(const IDrawingParser&)            = delete;
    IDrawingParser& operator=(const IDrawingParser&) = delete;

protected:
    // Only subclasses can construct
    IDrawingParser() = default;
};

// -------------------------------------------------------------------
//  ParserFactory
//  Single point of parser selection logic.
//  Inspects the file extension and returns
//  the appropriate concrete parser.
//
//  Usage:
//    auto parser = ParserFactory::create("shaft.dxf");
//    if (!parser)
//        // unsupported format
//    auto result = parser->parse("shaft.dxf");
// -------------------------------------------------------------------
class ParserFactory
{
public:
    // Returns the appropriate parser for the given file path.
    // Returns nullptr if no registered parser supports this format.
    static std::unique_ptr<IDrawingParser> create(
        const std::string& file_path);

    // Returns all file extensions supported across all parsers.
    // e.g. { ".dxf", ".dxb", ".pdf" }
    static std::vector<std::string> supported_formats();

    // Returns true if any registered parser can handle this file.
    static bool is_supported(const std::string& file_path);

private:
    // Not instantiable — all methods are static
    ParserFactory() = delete;
};

} // namespace Application