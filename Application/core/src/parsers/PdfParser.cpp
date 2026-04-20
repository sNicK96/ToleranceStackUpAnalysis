// PDF labels is broken down into small strings of characters. We can refer them as box of strings here.
// It's boxes get auto segregated by the poppler, and so, it is unlikely that we will get the exact string tokens as we want.
// Example :

//  ## The spatial clustering problem
//  140 ±0.100
//  This might come out of the poppler as 3 separate boxes.
//  box 1: "140"     at (150, 220)
//  box 2: "±"       at (178, 220)
//  box 3: "0.100"   at (185, 220)

//  ## The noise filter problem
//  A mechanical drawing has a lot of text that is NOT a dimension:
//  "MATERIAL: EN8 STEEL"    --> notes
//  "SCALE 1:1"              --> title block
//  "DWG-001"                --> drawing number
//  "Ra0.8"                  --> surface finish
//  "SECTION A-A"            --> view label
//  "140 ±0.100"             --> THIS is a dimension ----> This is what we want. Everything else is noise

//  I have to filter all of this using the following conditions.
//  Does the string contain a digit? (always)
//  Does it contain ±, +, -, /, or %%? (tolerance indicators)
//  Is the text height within a reasonable range? (dimension text is usually 2.5–5mm)
//  Is it short enough to be a dimension? (not a sentence)

#include "parsers/PdfParser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-page.h>
#include <poppler/cpp/poppler-rectangle.h>

namespace Application {

// ---------------------------------------------
//  Constructor
// ---------------------------------------------
PdfParser::PdfParser()
    : tol_parser_()
{
    reset_state();
}

void PdfParser::reset_state()
{
    stats_       = PdfParserStats{};
    dim_counter_ = 0;
    gdt_counter_ = 0;
    source_file_ = "";
}

// ---------------------------------------------
//  IDrawingParser - can_parse / validate
// ---------------------------------------------
bool PdfParser::can_parse(const std::string& file_path) const
{
    return get_extension(file_path) == ".pdf";
}

bool PdfParser::validate(const std::string& file_path) const
{
	// Check if file is of type .pdf
    if (!can_parse(file_path)) return false;

    // PDF files always start with the magic bytes "%PDF"
    std::ifstream f(file_path, std::ios::binary);
    if (!f.is_open()) return false;

    char header[5] = {0};
    f.read(header, 4);

    return std::string(header, 4) == "%PDF";
}

// ---------------------------------------------
//  parse()
// ---------------------------------------------
ParseResult PdfParser::parse(
    const std::string&  file_path,
    const ParseOptions& options)
{
    reset_state();
    source_file_ = file_path;

    // validate
    {
        if (std::ifstream f(file_path); !f.is_open())
            return ParseResult::fail(ParseError::file_not_found(file_path));
    }

    if (!validate(file_path))
        return ParseResult::fail(ParseError::unsupported_format(file_path));

    // open PDF with poppler
    const auto doc = poppler::document::load_from_file(file_path);
    if (!doc)
        return ParseResult::fail(ParseError::corrupted(file_path));

    const int total_pages = doc->pages();
    if (total_pages == 0)
        return ParseResult::fail(ParseError::corrupted(file_path));

    // determine which pages to parse
    // options.target_page == -1 means all pages
    int page_start = 0;
    int page_end   = total_pages - 1;

    if (options.target_page >= 0 &&
        options.target_page < total_pages)
    {
        page_start = options.target_page;
        page_end   = options.target_page;
    }

    // process each page
    DimensionSet result;
    result.source_file = file_path;

    for (int page_idx = page_start; page_idx <= page_end; ++page_idx)
    {
        // extract raw text boxes from this page
        auto boxes = extract_text_boxes(page_idx, file_path);
        stats_.text_boxes_extracted += static_cast<int>(boxes.size());

        if (boxes.empty()) continue;

        // cluster into annotation strings
        auto clusters = cluster_text_boxes(boxes);
        stats_.clusters_formed += static_cast<int>(clusters.size());

        // filter and build dimensions
        for (auto& cluster : clusters)
        {
            if (!passes_noise_filter(cluster, options))
            {
                stats_.clusters_rejected_noise++;
                continue;
            }

            stats_.clusters_passed_filter++;

            if (auto dim_opt = build_dimension(cluster, options); dim_opt.has_value())
            {
                result.dimensions.push_back(std::move(dim_opt.value()));
                stats_.dimensions_extracted++;
            }
        }

        stats_.pages_processed++;
    }

    // check we got something
    if (result.dimensions.empty())
        return ParseResult::fail(ParseError::no_dimensions(file_path));

    return ParseResult::ok(std::move(result));
}

// ---------------------------------------------
//  extract_text_boxes
//  Uses poppler to read all text on one page
//  with their bounding rectangles
// ---------------------------------------------
std::vector<PdfTextBox> PdfParser::extract_text_boxes(
    const int                 page_index,
    const std::string&  file_path)
{
    std::vector<PdfTextBox> result;

    // Open the document fresh for each page
    // (poppler page objects are cheap to create)
    const auto doc = poppler::document::load_from_file(file_path);
    if (!doc) return result;

    const auto page = doc->create_page(page_index);
    if (!page) return result;

    // poppler::page::text_list() returns a vector of
    // text_box objects - each with text + bounding rect
    const auto text_list = page->text_list();

    result.reserve(text_list.size());

    for (const auto& tb : text_list)
    {
        // poppler::text_box::text() returns poppler::ustring
        // Convert to std::string via UTF-8
        std::string text = tb.text().to_utf8();

        if (text.empty()) continue;

        // Bounding rectangle in PDF points (1pt = 1/72 inch)
        auto rect = tb.bbox();

        PdfTextBox box;
        box.text   = std::move(text);
        box.x      = rect.x();
        box.y      = rect.y();
        box.width  = rect.width();
        box.height = rect.height();
        box.page   = page_index;

        result.push_back(std::move(box));
    }

    return result;
}

// ---------------------------------------------
//  cluster_text_boxes
//  Groups nearby boxes on the same line into
//  PdfTextCluster objects
//
//  Algorithm:
//  1. Sort boxes left-to-right, top-to-bottom
//  2. Walk through boxes in order
//  3. If a box is on the same line as the
//     current group AND close enough in X,
//     add it to the current group
//  4. Otherwise, flush the current group as a
//     cluster and start a new one
// ---------------------------------------------
std::vector<PdfTextCluster> PdfParser::cluster_text_boxes(
    std::vector<PdfTextBox>& boxes) const
{
    std::vector<PdfTextCluster> clusters;
    if (boxes.empty()) return clusters;

    // sort
    sort_text_boxes(boxes);

    // walk and group
    std::vector<PdfTextBox> current_group;
    current_group.push_back(boxes[0]);

    for (std::size_t i = 1; i < boxes.size(); ++i)
    {
        const PdfTextBox& prev = current_group.back();
        const PdfTextBox& curr = boxes[i];

        // Same line check - vertical centers within threshold
        const bool same_line = std::fabs(curr.centre_y() - prev.centre_y())
                         < cluster_opts_.same_line_y_threshold;

        // Close enough horizontally - gap between prev right and curr left
        const double x_gap = curr.x - prev.right();
        const bool close_x = (x_gap >= 0.0 &&
                        x_gap < cluster_opts_.same_cluster_x_gap);

        if (same_line && close_x)
        {
            // Belongs to current group
            current_group.push_back(curr);
        }
        else
        {
            // Flush current group --> cluster
            if (!current_group.empty())
            {
                clusters.push_back(
                    merge_into_cluster(current_group, current_group[0].page));
                current_group.clear();
            }
            current_group.push_back(curr);
        }
    }

    // Flush the last group
    if (!current_group.empty())
    {
        clusters.push_back(
            merge_into_cluster(current_group, current_group[0].page));
    }

    return clusters;
}

// ---------------------------------------------
//  sort_text_boxes
//  Top-to-bottom, then left-to-right within
//  the same line. This ensures clustering
//  always processes boxes in reading order.
// ---------------------------------------------
void PdfParser::sort_text_boxes(std::vector<PdfTextBox>& boxes) const
{
    std::sort(boxes.begin(), boxes.end(),
        [this](const PdfTextBox& a, const PdfTextBox& b)
        {
            // Different lines - sort by Y (top of page first)
            // PDF Y coordinates increase upward - use > for top-first
            if (std::fabs(a.centre_y() - b.centre_y())
                > cluster_opts_.same_line_y_threshold)
            {
                return a.centre_y() > b.centre_y();
            }
            // Same line - sort by X (left to right)
            return a.x < b.x;
        });
}

// ---------------------------------------------
//  merge_into_cluster
//  Joins a group of boxes into one cluster
//  Merges text with a space separator
// ---------------------------------------------
PdfTextCluster PdfParser::merge_into_cluster(
    const std::vector<PdfTextBox>& group,
    const int page)
{
    PdfTextCluster cluster;
    cluster.boxes = group;
    cluster.page  = page;
    cluster.x     = group.front().x;
    cluster.y     = group.front().y;

    // Join all text with a single space between boxes
    std::ostringstream oss;
    for (std::size_t i = 0; i < group.size(); ++i)
    {
        if (i > 0) oss << ' ';
        oss << group[i].text;
    }
    cluster.merged_text = oss.str();

    return cluster;
}

// ---------------------------------------------
//  Phase 3 - passes_noise_filter
//  Returns true if a cluster is likely to be
//  a dimension annotation
//
//  A cluster is a dimension if ALL of:
//    1. Text height is above minimum threshold
//    2. Contains at least one digit
//    3. Contains a tolerance indicator
//    4. Is short enough to be an annotation
// ---------------------------------------------
bool PdfParser::passes_noise_filter(
    const PdfTextCluster&   cluster,
    const ParseOptions&     options) const
{
    // text height (noise filter for tiny labels)
    const double min_h = std::max(cluster_opts_.min_text_height,
                            options.min_text_height);
    if (cluster.avg_height() < min_h)
        return false;

    const std::string& text = cluster.merged_text;

    // must contain a digit
    if (!has_dimension_content(text))
        return false;

    // must contain a tolerance indicator
    if (!has_tolerance_indicator(text))
        return false;

    // must be short enough
    if (!is_reasonable_length(text))
        return false;

    return true;
}

bool PdfParser::has_dimension_content(const std::string& text)
{
    return std::any_of(text.begin(), text.end(), ::isdigit);
}

bool PdfParser::has_tolerance_indicator(const std::string& text)
{
    // Any of these signals a tolerance is present
    return text.find("±")   != std::string::npos ||
           text.find("%%p") != std::string::npos ||
           text.find("%%P") != std::string::npos ||
           text.find('+')   != std::string::npos ||
           text.find('^')   != std::string::npos ||
           text.find("%%v") != std::string::npos || // GD&T frame
           text.find("%%V") != std::string::npos;
}

bool PdfParser::is_reasonable_length(const std::string& text) const
{
    return text.length() <= cluster_opts_.max_cluster_length;
}

// ---------------------------------------------
//  build_dimension
//  Parses a filtered cluster into a Dimension
// ---------------------------------------------
std::optional<Dimension> PdfParser::build_dimension(
    const PdfTextCluster&   cluster,
    const ParseOptions&     options)
{
    const std::string& text = cluster.merged_text;

    // Parse tolerance string
    auto parse_result = tol_parser_.parse(text);

    if (!parse_result.success)
    {
        stats_.tolerance_parse_errors++;
        return std::nullopt;
    }

    // Apply unit scale
    // PDF drawings are usually already in mm
    // but respect options.expect_metric
    if (!options.expect_metric)
    {
        // Convert inches --> mm
        if (parse_result.nominal.has_value())
            parse_result.nominal.value() *= 25.4;
        parse_result.tolerance.upper *= 25.4;
        parse_result.tolerance.lower *= 25.4;
    }

    // Handle missing tolerance
    // If no tolerance was found in the string,
    // apply the general tolerance from options
    if (parse_result.tolerance.upper == 0.0 &&
        parse_result.tolerance.lower == 0.0 &&
        !parse_result.tolerance.is_gdt())
    {
        parse_result.tolerance = Tolerance::bilateral(
            options.general_tolerance);
    }

    // Determine dimension type
    DimensionType dim_type = parse_result.dim_type;

    // If it's a GD&T frame, override
    if (parse_result.tolerance.is_gdt())
        dim_type = DimensionType::GDT;

    // Build ID
    const std::string id = (dim_type == DimensionType::GDT)
                     ? next_gdt_id()
                     : next_dimension_id();

    if (dim_type == DimensionType::GDT)
        stats_.gdt_frames_extracted++;

    // Build label
    const std::string label = build_pdf_label(dim_type,
                                        parse_result.nominal,
                                        text);

    // Assemble Dimension
    Dimension dim;
    dim.id          = id;
    dim.label       = label;
    dim.dim_type    = dim_type;
    dim.nominal     = parse_result.nominal;
    dim.tolerance   = parse_result.tolerance;
    dim.source_file = source_file_;
    dim.source_page = cluster.page;
    dim.source_line = -1;               // PDF has no line numbers
    dim.direction   = ChainDirection::Undefined;
    dim.criticality = CriticalityLevel::Unassessed;

    return dim;
}

// ---------------------------------------------
//  build_pdf_label
//  Human-readable label for the dimensions table
// ---------------------------------------------
std::string PdfParser::build_pdf_label(
    const DimensionType                   dim_type,
    const std::optional<double>&    nominal,
    const std::string&              raw_text)
{
    std::ostringstream oss;

    switch (dim_type)
    {
        case DimensionType::Diameter: oss << "Dia "; break;
        case DimensionType::Radius:   oss << "Rad "; break;
        case DimensionType::Angular:  oss << "Ang "; break;
        case DimensionType::GDT:      oss << "GDT "; break;
        default:                      oss << "Lin "; break;
    }

    if (nominal.has_value())
        oss << nominal.value();

    // Append truncated raw text for context
    const std::string display = raw_text.substr(0, 30);
    oss << " [" << display << "]";

    return oss.str();
}

// ---------------------------------------------
//  ID generators
// ---------------------------------------------
std::string PdfParser::next_dimension_id()
{
    ++dim_counter_;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "D%02d", dim_counter_);
    return std::string(buf);
}

std::string PdfParser::next_gdt_id()
{
    ++gdt_counter_;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "G%02d", gdt_counter_);
    return std::string(buf);
}

// ---------------------------------------------
//  Utility
// ---------------------------------------------
// static
std::string PdfParser::get_extension(const std::string& file_path)
{
    const auto pos = file_path.rfind('.');
    if (pos == std::string::npos) return "";

    std::string ext = file_path.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](const unsigned char c){ return std::tolower(c); });
    return ext;
}

} // namespace Application
