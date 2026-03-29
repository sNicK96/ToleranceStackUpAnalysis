#pragma once

#include "parsers/IDrawingParser.h"
#include "models/Dimension.h"
#include "utils/ToleranceStringParser.h"
#include <string>
#include <vector>
#include <optional>

namespace Application {

// ------------------------------------------------------------------
//  PdfTextBox
//  One piece of text extracted from a PDF page
//  with its position and size on the page.
//
//  Poppler gives us these raw — we cluster,
//  filter, and parse them into Dimensions.
// ------------------------------------------------------------------
struct PdfTextBox
{
    std::string text;           // raw text content
    double      x       = 0.0; // left edge position (points)
    double      y       = 0.0; // bottom edge position (points)
    double      width   = 0.0; // bounding box width
    double      height  = 0.0; // bounding box height (≈ font size)
    int         page    = 0;   // 0-based page index

    // Right edge — used for horizontal clustering
    double right() const { return x + width; }

    // Vertical centre — used for same-line detection
    double centre_y() const { return y + height / 2.0; }
};

// ------------------------------------------------------------------
//  PdfTextCluster
//  A group of PdfTextBoxes that belong together
//  on the same line — merged into one string
//  before being passed to ToleranceStringParser.
//
//  Example:
//    box "140" + box "±" + box "0.100"
//    → cluster.merged_text = "140 ±0.100"
// ------------------------------------------------------------------
struct PdfTextCluster
{
    std::string         merged_text;    // all boxes joined with spaces
    std::vector<PdfTextBox> boxes;      // source boxes (for traceability)
    int                 page    = 0;
    double              x       = 0.0;  // leftmost x of all boxes
    double              y       = 0.0;  // y of first box

    // Average text height across all boxes in cluster
    // Used as a noise filter — tiny text is not a dimension
    double avg_height() const
    {
        if (boxes.empty()) return 0.0;
        double sum = 0.0;
        for (const auto& b : boxes) sum += b.height;
        return sum / boxes.size();
    }
};

// ------------------------------------------------------------------
//  PdfParserStats
//  Diagnostic counters from last parse() call
// ------------------------------------------------------------------
struct PdfParserStats
{
    int pages_processed         = 0;
    int text_boxes_extracted    = 0;
    int clusters_formed         = 0;
    int clusters_passed_filter  = 0;
    int dimensions_extracted    = 0;
    int gdt_frames_extracted    = 0;
    int tolerance_parse_errors  = 0;
    int clusters_rejected_noise = 0;
};

// ------------------------------------------------------------------
//  PdfClusteringOptions
//  Controls how text boxes are grouped
//  into clusters. Tuned for typical
//  mechanical drawing annotation sizes.
// ------------------------------------------------------------------
struct PdfClusteringOptions
{
    // Two boxes are on the same line if their
    // centre_y values differ by less than this
    double same_line_y_threshold    = 3.0;  // points

    // Two boxes on the same line are merged if
    // the gap between them is less than this
    double same_cluster_x_gap       = 12.0; // points

    // Minimum text height to consider as a dimension
    // Filters out tiny notes, hatching labels etc.
    double min_text_height          = 4.0;  // points (~1.4mm at 72dpi)

    // Maximum cluster text length
    // Longer strings are likely notes, not dimensions
    std::size_t max_cluster_length  = 40;   // characters
};

// ------------------------------------------------------------------
//  PdfParser
//  Parses vector PDF engineering drawings and
//  extracts dimensional annotations.
//
//  Depends on libpoppler-cpp for PDF reading.
//  All poppler types are kept in the .cpp —
//  this header has no poppler dependencies.
//
//  Usage:
//    PdfParser parser;
//    auto result = parser.parse("shaft.pdf");
//    if (result.success)
//        auto& dims = result.data.value();
// ------------------------------------------------------------------
class PdfParser : public IDrawingParser
{
public:
    PdfParser();
    ~PdfParser() override = default;

    // ── IDrawingParser interface

    ParseResult parse(
        const std::string&  file_path,
        const ParseOptions& options = ParseOptions{}) override;

    bool can_parse(const std::string& file_path) const override;

    std::string name() const override
    {
        return "PDF Parser v1.0 (poppler)";
    }

    std::vector<std::string> supported_extensions() const override
    {
        return { ".pdf" };
    }

    // Validates PDF by checking "%PDF" magic header bytes
    bool validate(const std::string& file_path) const override;

    //PDF-specific extras

    const PdfParserStats&       last_stats()            const { return stats_; }
    const PdfClusteringOptions& clustering_options()    const { return cluster_opts_; }

    // Allow caller to tune clustering before parsing
    void set_clustering_options(const PdfClusteringOptions& opts)
    {
        cluster_opts_ = opts;
    }

private:

    //Phase 1: Text extraction

    // Extracts all PdfTextBoxes from one page using poppler
    // Returns empty vector if page cannot be read
    std::vector<PdfTextBox> extract_text_boxes(
        int                 page_index,
        const std::string&  file_path) const;

    //Phase 2: Spatial clustering

    // Groups nearby text boxes on the same line
    // into PdfTextCluster objects
    std::vector<PdfTextCluster> cluster_text_boxes(
        std::vector<PdfTextBox>& boxes) const;

    // Sorts boxes left-to-right, top-to-bottom
    // Must run before clustering
    void sort_text_boxes(std::vector<PdfTextBox>& boxes) const;

    // Merges a group of boxes into one cluster
    PdfTextCluster merge_into_cluster(
        const std::vector<PdfTextBox>& group,
        int page) const;

    //Phase 3: Noise filtering

    // Returns true if a cluster is likely to be
    // a dimension annotation rather than a label or note
    bool passes_noise_filter(
        const PdfTextCluster&   cluster,
        const ParseOptions&     options) const;

    // Individual filter checks — separated for clarity
    bool has_dimension_content  (const std::string& text) const;
    bool has_tolerance_indicator(const std::string& text) const;
    bool is_reasonable_length   (const std::string& text) const;

    //Phase 4: Dimension building

    // Attempts to build a Dimension from a cluster
    // Returns nullopt if cluster is not a parseable dimension
    std::optional<Dimension> build_dimension(
        const PdfTextCluster&   cluster,
        const ParseOptions&     options);

    //Label builder
    std::string build_pdf_label(
        DimensionType                   dim_type,
        const std::optional<double>&    nominal,
        const std::string&              raw_text) const;

    //ID generation
    std::string next_dimension_id();
    std::string next_gdt_id();

    //Utility ───
    static std::string get_extension(const std::string& file_path);

    void reset_state();

    //Internal state
    ToleranceStringParser   tol_parser_;
    PdfParserStats          stats_;
    PdfClusteringOptions    cluster_opts_;
    int                     dim_counter_ = 0;
    int                     gdt_counter_ = 0;
    std::string             source_file_;
};

} // namespace Application
