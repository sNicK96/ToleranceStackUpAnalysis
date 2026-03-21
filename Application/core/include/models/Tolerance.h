// The Tolerance struct gives us one unified type that can represent all four cases cleanly, so the rest of the engine never has to worry about which format it came from.

#pragma once

#include <string>
#include <variant>
#include <optional>

namespace Application {

// --------------------------------------------------------------------
//  Describes how the tolerance is expressed
// --------------------------------------------------------------------
    // Tolerance can be expressed in majorly 4 types
    // ±0.025          → bilateral
    // +0.021 / -0.000 → unilateral
    // 29.980 / 30.021 → limits
    // ⌀ 0.01 A        → GD&T frame -> Geometric dimensioning and tolerancing (GD&T) is a system of symbols used on engineering drawings to communicate information from the designer to the manufacturer through engineering drawings. GD&T tells the manufacturer the degree of accuracy and precision needed for each controlled feature of the part.
enum class ToleranceType
{
    Bilateral,      // ± value  e.g. ±0.025
    Unilateral,     // +upper / -lower independently  e.g. +0.021 / -0.000
    Limit,          // expressed as min/max limits    e.g. 29.980 / 30.021
    GDT             // geometric dimensioning & tolerancing symbol
};

// --------------------------------------------------------------------
//  GD&T Characteristic Symbols
// --------------------------------------------------------------------
    // I'm following ASME Y14.5 which has 14 standard characteristic symbols.
    // I'm using this enum to specifically take care of these symbols only.
enum class GDTCharacteristic
{
    // Form
    Straightness,
    Flatness,
    Circularity,
    Cylindricity,S

    // Orientation
    Angularity,
    Perpendicularity,
    Parallelism,

    // Location
    Position,
    Concentricity,
    Symmetry,

    // Runout
    CircularRunout,
    TotalRunout,

    // Profile
    ProfileOfALine,
    ProfileOfASurface,

    Unknown
};

--------------------------------------------------------------------
//  GD&T Tolerance Frame
--------------------------------------------------------------------
    // This struct keeps the parsed GD notation accordingly
    // | ⌀ 0.01 | A | B |
    // This would parse into
    // diameter_symbol  = true
    // tolerance_value  = 0.01
    // datum_primary    = "A"
    // datum_secondary  = "B"
struct GDTFrame
{
    GDTCharacteristic   characteristic  = GDTCharacteristic::Unknown;
    double              tolerance_value = 0.0;              // e.g. 0.01
    bool                diameter_symbol = false;            // ⌀ prefix
    std::optional<std::string> datum_primary;               // e.g. "A"
    std::optional<std::string> datum_secondary;
    std::optional<std::string> datum_tertiary;

    std::string to_string() const;
};

// --------------------------------------------------------------------
//  Core Tolerance Struct
// --------------------------------------------------------------------
	// This takes the upper and lower limit of the notations and stores them accordingly.
struct Tolerance
{
    ToleranceType   type            = ToleranceType::Bilateral;

    // For Bilateral:   upper == lower == value
    // For Unilateral:  upper and lower set independently
    // For Limit:       upper = max, lower = min (absolute values)
    double          upper           = 0.0;
    double          lower           = 0.0;

    // GD&T frame — populated only when type == GDT
    std::optional<GDTFrame> gdt_frame;

    // ── Convenience constructors ──────────────

    // Bilateral:  ±value
    static Tolerance bilateral(double value)
    {
        Tolerance t;
        t.type  = ToleranceType::Bilateral;
        t.upper = value;
        t.lower = value;
        return t;
    }

    // Unilateral: +upper / -lower
    static Tolerance unilateral(double upper_val, double lower_val)
    {
        Tolerance t;
        t.type  = ToleranceType::Unilateral;
        t.upper = upper_val;
        t.lower = lower_val;
        return t;
    }

    // Limit: [min, max]
    static Tolerance limit(double min_val, double max_val)
    {
        Tolerance t;
        t.type  = ToleranceType::Limit;
        t.upper = max_val;
        t.lower = min_val;
        return t;
    }

    // GD&T frame
    static Tolerance gdt(GDTFrame frame)
    {
        Tolerance t;
        t.type      = ToleranceType::GDT;
        t.upper     = frame.tolerance_value;
        t.lower     = 0.0;
        t.gdt_frame = std::move(frame);
        return t;
    }

    // ── Helpers ───────────────────────────────

    // Returns the total tolerance band (upper + lower)
    double band() const { return upper + lower; }

    // Returns the worst-case deviation (max of upper/lower)
    double worst_case() const { return std::max(upper, lower); }

    bool is_gdt()       const { return type == ToleranceType::GDT; }
    bool is_bilateral() const { return type == ToleranceType::Bilateral; }

    std::string to_string() const;
};

}