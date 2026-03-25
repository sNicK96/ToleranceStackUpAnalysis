#pragma once

#include "parsers/IDrawingParser.h"
#include "models/Dimension.h"
#include "utils/ToleranceStringParser.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <optional>

namespace Application {

// ─────────────────────────────────────────────
//  DxfToken
//  One group code + value pair read from the
//  DXF file. The atomic unit of DXF parsing.
//
//  Every two lines in a DXF file form one token:
//    Line 1: group code (integer)
//    Line 2: value      (string — we convert later)
// ─────────────────────────────────────────────
struct DxfToken
{
    int         group_code  = 0;
    std::string value;

    // Convenience converters — DXF values are always
    // read as strings, then converted as needed
    double      as_double() const;
    int         as_int()    const;
    bool        is_empty()  const { return value.empty(); }
};

// ─────────────────────────────────────────────
//  DxfEntity
//  All group codes collected for one entity
//  before we process it into a Dimension.
//
//  A DIMENSION entity in DXF looks like:
//    0  → DIMENSION        (entity type)
//    8  → DIMENSIONS       (layer)
//    1  → "%%C30 +0.021"   (annotation text)
//   42  → 30.0             (measured value)
//   70  → 4                (dimension type flag)
//   10  → 155.0            (x of first point)
//   20  → 15.0             (y of first point)
//   ...
//
//  We collect all these into one DxfEntity,
//  then convert it to a Dimension in one shot.
// ─────────────────────────────────────────────
struct DxfEntity
{
    std::string entity_type;    // "DIMENSION", "TOLERANCE", "TEXT", "LINE" ...
    std::string layer;

    // All group codes for this entity
    // key = group code, value = raw string value
    // Using unordered_map — fast lookup by group code
    std::unordered_map<int, std::string> codes;

    // Source location — for traceability and error reporting
    int         start_line  = -1;

    // ── Convenience accessors ─────────────────
    // Returns empty string if code not present
    std::string get_string (int code) const;
    double      get_double (int code, double  default_val = 0.0) const;
    int         get_int    (int code, int     default_val = 0)   const;

    bool        has_code   (int code) const;
    bool        is_type    (const std::string& type) const;
};

// ─────────────────────────────────────────────
//  DxfSectionType
//  Which section of the DXF file we are in.
//  We skip most sections — only ENTITIES
//  and BLOCKS contain dimension data.
// ─────────────────────────────────────────────
enum class DxfSectionType
{
    None,
    Header,
    Tables,
    Blocks,
    Entities,
    Objects,
    Unknown
};

// ─────────────────────────────────────────────
//  DxfParserStats
//  Diagnostic counters — how many of each
//  entity type were seen and processed.
//  Useful for debugging and unit tests.
// ─────────────────────────────────────────────
struct DxfParserStats
{
    int total_lines_read        = 0;
    int entities_seen           = 0;
    int dimensions_extracted    = 0;
    int gdt_frames_extracted    = 0;
    int text_entities_seen      = 0;
    int entities_skipped        = 0;    // wrong layer, unsupported type etc.
    int tolerance_parse_errors  = 0;    // annotation strings we couldn't parse
};

// ─────────────────────────────────────────────
//  DxfParser
//  Parses DXF files and extracts dimensions,
//  tolerances, and GD&T annotations.
//
//  Usage:
//    DxfParser parser;
//    auto result = parser.parse("shaft.dxf");
//    if (result.success)
//        auto& dims = result.data.value();
// ─────────────────────────────────────────────
class DxfParser : public IDrawingParser
{
public:
    DxfParser();
    ~DxfParser() override = default;

    // ── IDrawingParser interface ──────────────

    ParseResult parse(
        const std::string&  file_path,
        const ParseOptions& options = ParseOptions{}) override;

    bool can_parse(const std::string& file_path) const override;

    std::string name() const override
    {
        return "DXF Parser v1.0";
    }

    std::vector<std::string> supported_extensions() const override
    {
        return { ".dxf", ".dxb" };
    }

    // Validates DXF by checking "AC10" magic header bytes
    bool validate(const std::string& file_path) const override;

    // ── DXF-specific extras ───────────────────

    // Returns stats from the last parse() call
    // Useful for debugging and unit tests
    const DxfParserStats& last_stats() const { return stats_; }

private:

    // ── Phase 1: File reading ─────────────────

    // Opens the file and reads all tokens into a flat list
    // Returns false if file cannot be opened
    bool read_tokens(const std::string&     file_path,
                     std::vector<DxfToken>& out_tokens);

    // ── Phase 2: Tokenise into entities ───────

    // Groups flat token list into DxfEntity objects
    // Each entity starts when group code 0 is seen
    std::vector<DxfEntity> tokenise_entities(
        const std::vector<DxfToken>& tokens);

    // ── Phase 3: Section routing ──────────────

    // Determines which DXF section a token belongs to
    DxfSectionType identify_section(const std::string& section_name) const;

    // Returns true if we should process entities in this section
    // We only care about ENTITIES and BLOCKS
    bool should_process_section(DxfSectionType section) const;

    // ── Phase 4: Entity processing ────────────

    // Master dispatcher — routes entity to correct handler
    // Returns nullopt if entity should be skipped
    std::optional<Dimension> process_entity(
        const DxfEntity&    entity,
        const ParseOptions& options);

    // Handlers for each entity type we care about
    std::optional<Dimension> process_dimension  (const DxfEntity& entity);
    std::optional<Dimension> process_tolerance  (const DxfEntity& entity);  // GD&T frames
    std::optional<Dimension> process_text       (const DxfEntity& entity);  // fallback text dims

    // ── Phase 5: Dimension type resolution ────

    // Converts DXF dimension type flag (group code 70)
    // to our DimensionType enum
    DimensionType resolve_dimension_type(int dxf_flag) const;

    // ── Layer filtering ───────────────────────

    // Returns true if this entity's layer passes the filter
    bool passes_layer_filter(const DxfEntity&    entity,
                             const ParseOptions& options) const;

    // ── Header reading ────────────────────────

    // Reads $INSUNITS from HEADER section
    // Sets internal unit scale factor (mm vs inches)
    void read_header_variables(const std::vector<DxfEntity>& entities);

    // ── ID generation ─────────────────────────

    // Generates sequential IDs: D01, D02, D03 ...
    // G01, G02 ... for GD&T entries
    std::string next_dimension_id();
    std::string next_gdt_id();

    // ── Utility ───────────────────────────────

    // Strips DXF special codes from annotation strings
    // %%C → Ø,  %%P → ±,  %%D → °
    static std::string decode_dxf_symbols(const std::string& raw);

    // Extracts file extension, lowercased
    static std::string get_extension(const std::string& file_path);

    // ── Internal state ────────────────────────
    // Reset at the start of every parse() call

    ToleranceStringParser   tol_parser_;    // reused across entities
    DxfParserStats          stats_;
    double                  unit_scale_;    // 1.0 for mm, 25.4 for inches→mm
    int                     dim_counter_;   // for D01, D02 ...
    int                     gdt_counter_;   // for G01, G02 ...
    std::string             source_file_;   // current file being parsed

    void reset_state();
};

} // namespace Application
