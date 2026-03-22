// This interface is an abstract that gets implemented by both DxfParser and PdfParser. This way the rest of the engine never cares which format it's reading.
// ParserFactory::create("shaft.dxf")
//         │
//         │  returns unique_ptr<IDrawingParser>
//         │  (actually a DxfParser underneath)
//         ▼
// IDrawingParser::parse("shaft.dxf", options)
//         │
//         │  returns ParseResult
//         │  ├── success = true
//         │  └── data = DimensionSet { D01, D02, D03, D04, D05, G01 }
// 		▼
// ChainFinder::find_chains(dimension_set)
// 		│
// 		▼
// ... rest of engine
#pragma once

#include "models/Dimension.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace Application {

    // -------------------------------------------------------------------
    //  ParseError
    //  Structured error returned when parsing fails
    //  Instead of throwing exceptions we return this
    // -------------------------------------------------------------------
    struct ParseError
    {
        enum class Code
        {
            FileNotFound,
            UnsupportedFormat,
            CorruptedFile,
            EmptyFile,
            NoDimensionsFound,
            PartialParse        // parsed but with warnings
        };

        Code        code;
        std::string message;        // human readable description
        std::string source_file;
        int         line = -1;      // line/entity where error occurred (-1 if unknown)

        std::string to_string() const;
    };

    // -------------------------------------------------------------------
    //  ParseResult
    //  A discriminated union — either a DimensionSet
    //  OR a ParseError. Never both, never neither.
    // -------------------------------------------------------------------
    struct ParseResult
    {
        bool                        success = false;
        std::optional<DimensionSet> data;           // valid only when success = true
        std::optional<ParseError>   error;          // valid only when success = false

        // Named constructors — clear intent at call site
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
    };
    
    // -------------------------------------------------------------------
    //  ParseOptions
    //  Configuration passed into the parser
    //  Controls what gets extracted
    //  A quick analysis might only need diameters, but a full GD&T analysis needs everything.
    // -------------------------------------------------------------------
    struct ParseOptions
    {
        bool    extract_linear_dims     = true;
        bool    extract_diameter_dims   = true;
        bool    extract_angular_dims    = true;
        bool    extract_gdt             = true;
        bool    extract_surface_finish  = false;    // Phase 2+ extension

        // Layer filter - if non-empty, only parse these layers
        // If empty, parse all layers
        // I want to target only DIMENSIONS, GDT, ANNOTATIONS etc. rather than parsing noise from CONSTRUCTION, HIDDEN, HATCH layers.
        std::vector<std::string> layer_filter;

        // Tolerance string format hints
        bool    expect_metric           = true;     // mm vs inches
        double  general_tolerance       = 0.1;      // fallback if no tolerance specified
    };
    
    // -------------------------------------------------------------------
    //  IDrawingParser
    //  Abstract base class — all parsers implement this
    // -------------------------------------------------------------------
    class IDrawingParser
    {
    public:
        virtual ~IDrawingParser() = default;

        // ── Core interface — must implement ──────

        // Parse a drawing file and return all extracted dimensions
        virtual ParseResult parse(
            const std::string&  file_path,
            const ParseOptions& options = ParseOptions{}) = 0;

        // Returns true if this parser can handle the given file
        // Based on extension + optionally file header inspection
        virtual bool can_parse(const std::string& file_path) const = 0;

        // Human readable name — e.g. "DXF Parser v1.0"
        virtual std::string name() const = 0;

        // ── Optional overrides ───────────────────

        // Validate file without full parse — fast pre-check
        // Default implementation just calls can_parse()
        virtual bool validate(const std::string& file_path) const
        {
            return can_parse(file_path);
        }

        // Returns supported file extensions e.g. {".dxf", ".dxb"}
        virtual std::vector<std::string> supported_extensions() const = 0;

        // ── Non-copyable, non-movable ─────────────
        IDrawingParser(const IDrawingParser&)            = delete;
        IDrawingParser& operator=(const IDrawingParser&) = delete;

    protected:
        IDrawingParser() = default;
    };
    
    // -------------------------------------------------------------------
    //  ParserFactory
    //  Creates the right parser for a given file
    //  Single point of parser selection logic
    // -------------------------------------------------------------------
    class ParserFactory
    {
    public:
        // Returns the appropriate parser for the file extension
        // Returns nullptr if no parser supports this file type
        static std::unique_ptr<IDrawingParser> create(
            const std::string& file_path);

        // Returns all registered parsers
        static std::vector<std::string> supported_formats();
    };

} // namespace Application
