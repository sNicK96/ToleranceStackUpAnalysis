#include "utils/ToleranceStringParser.h"
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace Application {

// -------------------------------------------------------------------
//  GD&T characteristic string → enum map
//  DXF group code 3 on TOLERANCE entities contains these strings
// -------------------------------------------------------------------
    // I'm using a few aliases because the entities can be named different by different CAD tools, so I want to make the application more stable to different GD&T inputs.
static const std::unordered_map<std::string, GDTCharacteristic>
k_gdt_char_map =
{
    // Form
    { "STRAIGHTNESS",       GDTCharacteristic::Straightness       },
    { "FLATNESS",           GDTCharacteristic::Flatness           },
    { "CIRCULARITY",        GDTCharacteristic::Circularity        },
    { "ROUNDNESS",          GDTCharacteristic::Circularity        }, // alias
    { "CYLINDRICITY",       GDTCharacteristic::Cylindricity       },

    // Orientation
    { "ANGULARITY",         GDTCharacteristic::Angularity         },
    { "PERPENDICULARITY",   GDTCharacteristic::Perpendicularity   },
    { "SQUARENESS",         GDTCharacteristic::Perpendicularity   }, // alias
    { "PARALLELISM",        GDTCharacteristic::Parallelism        },

    // Location
    { "POSITION",           GDTCharacteristic::Position           },
    { "CONCENTRICITY",      GDTCharacteristic::Concentricity      },
    { "SYMMETRY",           GDTCharacteristic::Symmetry           },

    // Runout
    { "RUNOUT",             GDTCharacteristic::CircularRunout     },
    { "CIRCULAR RUNOUT",    GDTCharacteristic::CircularRunout     },
    { "TOTAL RUNOUT",       GDTCharacteristic::TotalRunout        },

    // Profile
    { "PROFILE OF A LINE",    GDTCharacteristic::ProfileOfALine    },
    { "PROFILE OF A SURFACE", GDTCharacteristic::ProfileOfASurface },
};

// -------------------------------------------------------------------
//  Constructor — compile all regex patterns
//  Expensive to compile, so do it once here
//  and reuse on every parse() call
// -------------------------------------------------------------------
ToleranceStringParser::ToleranceStringParser()
{
    // --- Bilateral: "140 ±0.100" or "140 %%p0.100" ---
    // Captures: (1) nominal   (2) tolerance value
    // ±  or %%p or %%P before the tolerance number
    re_bilateral_ = std::regex(
        R"(([+-]?\d+\.?\d*)\s*[±%%Pp]{1,3}(\d+\.?\d*))",
        std::regex::icase
    );

    // --- Unilateral caret: "+0.021^-0.000" --------------
    // Captures: (1) upper sign+value   (2) lower sign+value
    // The caret ^ separates stacked upper/lower in DXF
    re_unilateral_ = std::regex(
        R"(([+-]\d+\.?\d*)\^([+-]\d+\.?\d*))"
    );

    // --- Unilateral slash: "+0.021/-0.000" --------------
    // Captures: (1) upper sign+value   (2) lower sign+value
    // Used in PDF annotations and some DXF exports
    // Note: matched AFTER limit to avoid ambiguity
    re_unilateral_slash_ = std::regex(
        R"(([+-]\d+\.?\d*)\/([+-]\d+\.?\d*))"
    );

    // --- Limit: "29.980/30.021" ----------------------------
    // Two unsigned numbers separated by /
    // No leading +/- signs (that distinguishes it from unilateral slash)
    re_limit_ = std::regex(
        R"(^(\d+\.?\d*)\/(\d+\.?\d*)$)"
    );

    // --- Nominal extraction ----------------------------
    // Extracts the leading number from a string
    // Handles optional Ø prefix (after decode)
    // Captures: (1) the nominal number
    re_nominal_ = std::regex(
        R"(Ø?\s*([+-]?\d+\.?\d*))"
    );

    // --- Diameter detection -------------------------------------
    // True if string starts with Ø (after decode) or %%C (before decode)
    re_diameter_ = std::regex(
        R"((Ø|%%[Cc]))"
    );

    // --- GD&T separator ------------------------------------------
    // Splits GD&T frame string on %%v delimiter
    re_gdt_separator_ = std::regex(R"(%%[Vv])");
}

// -------------------------------------------------------------------
//  decode_dxf_symbols
//  Converts DXF escape codes to readable chars
// -------------------------------------------------------------------
// static
std::string ToleranceStringParser::decode_dxf_symbols(const std::string& raw)
{
    std::string result;
    result.reserve(raw.size());

    std::size_t i = 0;
    while (i < raw.size())
    {
        // Check for %% escape sequences
        if (i + 1 < raw.size() && raw[i] == '%' && raw[i+1] == '%')
        {
            if (i + 2 < raw.size())
            {
                // change each char to lower so the dxf commands can be case independent
                char code = std::tolower(raw[i+2]);
                // We use switch case specifically to preserve the symbols that are tricky to manage, if done by regex.
                switch (code)
                {
                    case 'c':               // %%C or %%c → Ø (diameter symbol)
                        result += "Ø";
                        i += 3;
                        continue;

                    case 'p':               // %%P or %%p → ± (plus-minus symbol)
                        result += "±";
                        i += 3;
                        continue;

                    case 'd':               // %%D or %%d → ° (degree symbol)
                        result += "°";
                        i += 3;
                        continue;

                    case 'v':               // %%V or %%v → GD&T separator, keep as-is
                        result += "%%v";    // preserve for GD&T frame splitting
                        i += 3;
                        continue;

                    default:
                        break;              // unknown %% code — pass through
                }
            }
        }

        result += raw[i];
        ++i;
    }

    return result;
}

// -------------------------------------------------------------------
//  is_diameter / is_gdt_frame
// -------------------------------------------------------------------
// static
bool ToleranceStringParser::is_diameter(const std::string& raw)
{
    return raw.find("Ø")   != std::string::npos ||
           raw.find("%%C") != std::string::npos ||
           raw.find("%%c") != std::string::npos;
}

// static
bool ToleranceStringParser::is_gdt_frame(const std::string& raw)
{
    return raw.find("%%v") != std::string::npos ||
           raw.find("%%V") != std::string::npos;
}

// -------------------------------------------------------------------
//  parse — master dispatcher
//  Tries formats in priority order:
//    1. GD&T frame    (%%v separator — unambiguous)
//    2. Bilateral     (± or %%p present)
//    3. Unilateral ^  (^ separator — DXF stacked)
//    4. Limit         (two unsigned numbers / — no signs)
//    5. Unilateral /  (signed numbers / )
//    6. Nominal only  (bare number — use general tolerance)
// -------------------------------------------------------------------
ToleranceParseResult ToleranceStringParser::parse(const std::string& raw) const
{
    if (raw.empty())
    {
        ToleranceParseResult r;
        r.success       = false;
        r.raw_string    = raw;
        r.error_message = "Empty annotation string";
        return r;
    }

    // Store original for traceability
    // Then decode DXF symbols for matching
    std::string decoded = decode_dxf_symbols(raw);

    // Trim leading/trailing whitespace
    auto ltrim = decoded.find_first_not_of(" \t\r\n");
    auto rtrim = decoded.find_last_not_of(" \t\r\n");
    if (ltrim != std::string::npos)
        decoded = decoded.substr(ltrim, rtrim - ltrim + 1);

    // --- Priority 1: GD&T frame -----------------------─
    if (is_gdt_frame(raw))
    {
        auto result = parse_gdt_frame(raw);
        result.raw_string = raw;
        return result;
    }

    // --- Priority 2: Bilateral --------------------------
    if (decoded.find('±') != std::string::npos)
    {
        auto result = parse_bilateral(decoded);
        result.raw_string = raw;
        return result;
    }

    // --- Priority 3: Unilateral caret (DXF stacked) ---
    if (decoded.find('^') != std::string::npos)
    {
        auto result = parse_unilateral(decoded);
        result.raw_string = raw;
        return result;
    }

    // --- Priority 4: Limit ("29.980/30.021") ----
    {
        std::smatch m;
        // Limit: strip Ø prefix if present, then try bare number/number
        std::string stripped = decoded;
        if (!stripped.empty() && stripped[0] == 'O') // Ø decoded to letter on some systems
            stripped = stripped.substr(1);
        // Remove Ø (UTF-8: 2 bytes C3 98)
        auto oslash = stripped.find("Ø");
        if (oslash != std::string::npos)
            stripped = stripped.substr(oslash + 2); // skip 2-byte UTF-8 Ø

        if (std::regex_match(stripped, m, re_limit_))
        {
            auto result = parse_limit(decoded);
            result.raw_string = raw;
            return result;
        }
    }

    // --- Priority 5: Unilateral slash --------------
    if (decoded.find('/') != std::string::npos)
    {
        auto result = parse_unilateral(decoded);
        result.raw_string = raw;
        return result;
    }

    // --- Priority 6: Nominal only --------------------
    {
        std::smatch m;
        if (std::regex_search(decoded, m, re_nominal_))
        {
            ToleranceParseResult r;
            r.success  = true;
            r.nominal  = std::stod(m[1].str());
            r.dim_type = is_diameter(raw)
                         ? DimensionType::Diameter
                         : DimensionType::Linear;
            // No tolerance found — caller uses general_tolerance
            r.tolerance    = Tolerance::bilateral(0.0);
            r.raw_string   = raw;
            r.error_message = "No tolerance found — general tolerance will apply";
            return r;
        }
    }

    // --- Nothing matched ----------------------------------
    ToleranceParseResult r;
    r.success       = false;
    r.raw_string    = raw;
    r.error_message = "Could not parse annotation string: [" + raw + "]";
    return r;
}

// -------------------------------------------------------------------
//  parse_bilateral
//  Handles: "140 ±0.100"
// -------------------------------------------------------------------
ToleranceParseResult ToleranceStringParser::parse_bilateral(
    const std::string& decoded) const
{
    ToleranceParseResult r;
    r.raw_string = decoded;

    std::smatch m;
    if (!std::regex_search(decoded, m, re_bilateral_))
    {
        r.success       = false;
        r.error_message = "bilateral: no match in [" + decoded + "]";
        return r;
    }

    r.success   = true;
    r.nominal   = std::stod(m[1].str());
    double tol  = std::stod(m[2].str());
    r.tolerance = Tolerance::bilateral(tol);
    r.dim_type  = is_diameter(decoded)
                  ? DimensionType::Diameter
                  : DimensionType::Linear;
    return r;
}

// -------------------------------------------------------------------
//  parse_unilateral
//  Handles both:
//    "%%C30 +0.021^-0.000"   (caret, DXF)
//    "30 +0.021/-0.000"      (slash, PDF)
// -------------------------------------------------------------------
ToleranceParseResult ToleranceStringParser::parse_unilateral(
    const std::string& decoded) const
{
    ToleranceParseResult r;
    r.raw_string = decoded;

    // --- Extract nominal first --------------------------
    {
        std::smatch nm;
        if (std::regex_search(decoded, nm, re_nominal_))
            r.nominal = std::stod(nm[1].str());
    }

    // --- Try caret format: "+0.021^-0.000" ------─
    {
        std::smatch m;
        if (std::regex_search(decoded, m, re_unilateral_))
        {
            double upper = std::stod(m[1].str()); // signed
            double lower = std::stod(m[2].str()); // signed

            // upper should be positive deviation, lower should be negative
            // Store as absolute magnitudes — sign is conveyed by the field
            r.success   = true;
            r.tolerance = Tolerance::unilateral(
                std::abs(upper),
                std::abs(lower));
            r.dim_type  = is_diameter(decoded)
                          ? DimensionType::Diameter
                          : DimensionType::Linear;
            return r;
        }
    }

    // --- Try slash format: "+0.021/-0.000" ------─
    {
        std::smatch m;
        if (std::regex_search(decoded, m, re_unilateral_slash_))
        {
            double upper = std::stod(m[1].str());
            double lower = std::stod(m[2].str());

            r.success   = true;
            r.tolerance = Tolerance::unilateral(
                std::abs(upper),
                std::abs(lower));
            r.dim_type  = is_diameter(decoded)
                          ? DimensionType::Diameter
                          : DimensionType::Linear;
            return r;
        }
    }

    r.success       = false;
    r.error_message = "unilateral: no match in [" + decoded + "]";
    return r;
}

// -------------------------------------------------------------------
//  parse_limit
//  Handles: "29.980/30.021"
// -------------------------------------------------------------------
ToleranceParseResult ToleranceStringParser::parse_limit(
    const std::string& decoded) const
{
    ToleranceParseResult r;
    r.raw_string = decoded;

    // Strip Ø prefix if present
    std::string stripped = decoded;
    auto oslash = stripped.find("Ø");
    if (oslash != std::string::npos)
        stripped = stripped.substr(oslash + 2);

    // Trim whitespace
    auto ltrim = stripped.find_first_not_of(" \t");
    if (ltrim != std::string::npos)
        stripped = stripped.substr(ltrim);

    std::smatch m;
    if (!std::regex_match(stripped, m, re_limit_))
    {
        r.success       = false;
        r.error_message = "limit: no match in [" + decoded + "]";
        return r;
    }

    double val1 = std::stod(m[1].str());
    double val2 = std::stod(m[2].str());

    // Ensure val1 < val2 regardless of order in string
    double lo = std::min(val1, val2);
    double hi = std::max(val1, val2);

    r.success   = true;
    r.nominal   = (lo + hi) / 2.0;     // midpoint as nominal
    r.tolerance = Tolerance::limit(lo, hi);
    r.dim_type  = is_diameter(decoded)
                  ? DimensionType::Diameter
                  : DimensionType::Linear;
    return r;
}

// -------------------------------------------------------------------
//  parse_gdt_frame
//  Handles: "%%v0.010%%v%%v%%vA%%v"
//
//  GD&T frame field layout after splitting on %%v:
//  Index  Meaning
//  --------------------------------------------------------------─
//   0     (empty — before first %%v)
//   1     Tolerance value e.g. "0.010"
//   2     Material condition modifier (M, L, or empty)
//   3     (shape / zone descriptor — often empty)
//   4     Datum primary   e.g. "A"
//   5     Datum secondary e.g. "B"  (if present)
//   6     Datum tertiary  e.g. "C"  (if present)
// -------------------------------------------------------------------
ToleranceParseResult ToleranceStringParser::parse_gdt_frame(
    const std::string& raw) const
{
    ToleranceParseResult r;
    r.raw_string = raw;
    r.dim_type   = DimensionType::GDT;

    // Split on %%v (keep original — don't decode, we split on raw %%v)
    std::vector<std::string> fields;
    {
        std::sregex_token_iterator it(
            raw.begin(), raw.end(),
            re_gdt_separator_, -1);
        std::sregex_token_iterator end;
        for (; it != end; ++it)
            fields.push_back(it->str());
    }

    if (fields.size() < 2)
    {
        r.success       = false;
        r.error_message = "gdt_frame: too few fields in [" + raw + "]";
        return r;
    }

    GDTFrame frame;

    // --- Field 1: tolerance value --------------------─
    std::string tol_str = fields.size() > 1 ? fields[1] : "";

    // Tolerance may have %%C prefix (diameter zone)
    if (!tol_str.empty() && (tol_str.substr(0,3) == "%%C" ||
                              tol_str.substr(0,3) == "%%c"))
    {
        frame.diameter_symbol = true;
        tol_str = tol_str.substr(3);
    }

    if (tol_str.empty())
    {
        r.success       = false;
        r.error_message = "gdt_frame: empty tolerance field in [" + raw + "]";
        return r;
    }

    try
    {
        frame.tolerance_value = std::stod(tol_str);
    }
    catch (const std::exception&)
    {
        r.success       = false;
        r.error_message = "gdt_frame: could not parse tolerance value [" + tol_str + "]";
        return r;
    }

    // --- Field 4: datum primary -----------------------─
    if (fields.size() > 4 && !fields[4].empty())
        frame.datum_primary = fields[4];

    // --- Field 5: datum secondary --------------------─
    if (fields.size() > 5 && !fields[5].empty())
        frame.datum_secondary = fields[5];

    // --- Field 6: datum tertiary -----------------------
    if (fields.size() > 6 && !fields[6].empty())
        frame.datum_tertiary = fields[6];

    // --- Characteristic: comes from group code 3 ---
    // In DXF, the GD&T characteristic string is in
    // group code 3 of the TOLERANCE entity, NOT in
    // the %%v frame string itself.
    // We default to Unknown here — DxfParser will
    // set it from group code 3 after calling us.
    frame.characteristic = GDTCharacteristic::Unknown;

    r.success   = true;
    r.nominal   = std::nullopt;     // GD&T has no nominal
    r.tolerance = Tolerance::gdt(frame);
    return r;
}

// -------------------------------------------------------------------
//  resolve_gdt_characteristic
//  Maps DXF group code 3 string → enum
// -------------------------------------------------------------------
GDTCharacteristic ToleranceStringParser::resolve_gdt_characteristic(
    const std::string& dxf_str) const
{
    // Uppercase for case-insensitive match
    std::string upper = dxf_str;
    std::transform(upper.begin(), upper.end(), upper.begin(),
        [](unsigned char c){ return std::toupper(c); });

    auto it = k_gdt_char_map.find(upper);
    if (it != k_gdt_char_map.end())
        return it->second;

    return GDTCharacteristic::Unknown;
}

} // namespace Application
