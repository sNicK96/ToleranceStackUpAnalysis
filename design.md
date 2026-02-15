# Design Document: Tolerance Stack-Up Analysis System

## 1. System Overview

The Tolerance Stack-Up Analysis System is designed to automate the extraction, analysis, and calculation of dimensional tolerances from engineering drawings. The system consists of four main modules that work together to parse drawings, identify tolerance chains, perform stack-up calculations, and infer engineering intent.

## 2. Architecture

### 2.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     User Interface Layer                     │
│              (CLI / API / Web Interface)                     │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│                   Application Layer                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │   Drawing    │  │  Tolerance   │  │  Engineering │     │
│  │   Parser     │─▶│    Chain     │─▶│    Intent    │     │
│  │              │  │  Analyzer    │  │   Inference  │     │
│  └──────────────┘  └──────┬───────┘  └──────────────┘     │
│                            │                                 │
│                    ┌───────▼───────┐                        │
│                    │   Stack-Up    │                        │
│                    │  Calculator   │                        │
│                    └───────────────┘                        │
└─────────────────────────────────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│                     Data Layer                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │  Dimension   │  │  Tolerance   │  │   Analysis   │     │
│  │    Model     │  │    Chain     │  │    Results   │     │
│  │              │  │    Model     │  │              │     │
│  └──────────────┘  └──────────────┘  └──────────────┘     │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Module Descriptions

#### 2.2.1 Drawing Parser Module
Responsible for extracting dimensional and tolerance information from engineering drawings.

#### 2.2.2 Tolerance Chain Analyzer Module
Identifies and traces tolerance chains through assemblies.

#### 2.2.3 Stack-Up Calculator Module
Performs worst-case and statistical tolerance calculations.

#### 2.2.4 Engineering Intent Inference Module
Analyzes drawings to identify critical dimensions and design priorities.

## 3. Data Models

### 3.1 Dimension Model

```python
class Dimension:
    id: str                          # Unique identifier
    nominal_value: float             # Nominal dimension value
    unit: str                        # Unit of measurement (mm, inch, etc.)
    tolerance_type: ToleranceType    # Bilateral, Unilateral, GD&T
    upper_tolerance: float           # Upper deviation
    lower_tolerance: float           # Lower deviation
    feature_from: str                # Starting feature reference
    feature_to: str                  # Ending feature reference
    direction: Vector3D              # Direction vector
    metadata: dict                   # Additional properties
```

### 3.2 Tolerance Chain Model

```python
class ToleranceChain:
    id: str                          # Unique identifier
    dimensions: List[ChainDimension] # Ordered list of dimensions
    start_feature: str               # Chain starting point
    end_feature: str                 # Chain ending point
    total_nominal: float             # Sum of nominal values
    
class ChainDimension:
    dimension: Dimension             # Reference to dimension
    direction_multiplier: float      # +1 (additive) or -1 (subtractive)
```

### 3.3 Stack-Up Result Model

```python
class StackUpResult:
    chain_id: str                    # Reference to tolerance chain
    worst_case_min: float            # Minimum worst-case value
    worst_case_max: float            # Maximum worst-case value
    worst_case_range: float          # Total worst-case variation
    statistical_mean: float          # Statistical mean
    statistical_std_dev: float       # Statistical standard deviation
    statistical_range_6sigma: float  # ±3σ range
    method: CalculationMethod        # Worst-case or RSS
```

### 3.4 Critical Dimension Model

```python
class CriticalDimension:
    dimension: Dimension             # Reference to dimension
    criticality_score: float         # 0.0 to 1.0
    reason: CriticalityReason        # Why it's critical
    confidence: float                # Confidence in assessment (0.0 to 1.0)
    design_pattern: Optional[str]    # Identified pattern (bearing fit, etc.)
```

## 4. Module Design

### 4.1 Drawing Parser Module

#### 4.1.1 Components

- **PDF Parser**: Extracts text and graphics from PDF drawings
- **DXF Parser**: Parses DXF CAD files
- **Dimension Extractor**: Identifies dimension annotations
- **GD&T Interpreter**: Interprets geometric tolerancing symbols
- **Validator**: Validates extracted data

#### 4.1.2 Key Algorithms

**Dimension Extraction Algorithm:**
1. Parse drawing file format (PDF/DXF)
2. Identify dimension lines and arrows
3. Extract dimension text and values
4. Parse tolerance notation (±, +/-, GD&T)
5. Associate dimensions with features
6. Validate and structure data

#### 4.1.3 Error Handling

- Unsupported file format → Return error with supported formats
- Corrupted file → Return descriptive error message
- Ambiguous dimensions → Flag for manual review
- Missing tolerance → Use default or flag as incomplete

### 4.2 Tolerance Chain Analyzer Module

#### 4.2.1 Components

- **Feature Graph Builder**: Creates graph of dimensional relationships
- **Path Finder**: Identifies all paths between features
- **Chain Validator**: Validates chain continuity
- **Direction Analyzer**: Determines additive/subtractive directions

#### 4.2.2 Key Algorithms

**Tolerance Chain Identification Algorithm:**
1. Build feature relationship graph from dimensions
2. For each pair of target features:
   - Use graph traversal (BFS/DFS) to find all paths
   - Convert paths to tolerance chains
   - Determine direction multipliers for each dimension
3. Validate chain continuity
4. Return all distinct chains

#### 4.2.3 Graph Representation

```python
class FeatureGraph:
    nodes: Dict[str, Feature]        # Feature nodes
    edges: List[DimensionEdge]       # Dimension edges
    
class DimensionEdge:
    dimension: Dimension
    from_feature: str
    to_feature: str
    bidirectional: bool
```

### 4.3 Stack-Up Calculator Module

#### 4.3.1 Components

- **Worst-Case Calculator**: Computes arithmetic tolerance stack-up
- **Statistical Calculator**: Computes RSS tolerance stack-up
- **Tolerance Converter**: Handles different tolerance formats

#### 4.3.2 Key Algorithms

**Worst-Case Calculation:**
```
For each dimension in chain:
    If additive: 
        max_sum += nominal + upper_tolerance
        min_sum += nominal - lower_tolerance
    If subtractive:
        max_sum += nominal - lower_tolerance
        min_sum += nominal + upper_tolerance

worst_case_range = max_sum - min_sum
```

**Statistical (RSS) Calculation:**
```
nominal_sum = sum(dimension.nominal * direction_multiplier)

variance_sum = 0
For each dimension in chain:
    tolerance_range = (upper_tolerance + abs(lower_tolerance)) / 2
    variance_sum += tolerance_range²

std_dev = sqrt(variance_sum)
statistical_range = ±3 * std_dev  # 99.7% confidence
```

#### 4.3.3 Tolerance Type Support

- Bilateral symmetric: ±0.01
- Bilateral asymmetric: +0.02/-0.01
- Unilateral: +0.03/0 or 0/-0.03
- GD&T: Convert to equivalent bilateral

### 4.4 Engineering Intent Inference Module

#### 4.4.1 Components

- **Tolerance Analyzer**: Identifies tight tolerances
- **Fit Analyzer**: Identifies clearance/interference conditions
- **Pattern Recognizer**: Recognizes common design patterns
- **Criticality Scorer**: Assigns criticality scores

#### 4.4.2 Key Algorithms

**Critical Dimension Identification:**
```
For each dimension:
    score = 0.0
    
    # Tight tolerance indicator
    tolerance_ratio = tolerance_range / nominal_value
    if tolerance_ratio < threshold_tight:
        score += 0.4
        
    # Fit condition indicator
    if dimension in fit_controlling_dimensions:
        score += 0.3
        
    # Pattern recognition
    if dimension matches known_pattern:
        score += 0.3
        
    confidence = calculate_confidence(dimension, context)
    
    if score > criticality_threshold:
        mark as critical with score and confidence
```

**Design Pattern Recognition:**
- Bearing fits: Shaft/hole dimensions with H7/g6 type tolerances
- Threaded connections: Dimensions near thread callouts
- Alignment features: Dimensions with position tolerances
- Mating surfaces: Dimensions controlling part interfaces

#### 4.4.3 Confidence Scoring

Confidence based on:
- Data quality (0.0-0.3): Completeness of dimension data
- Context clarity (0.0-0.4): Clear feature relationships
- Pattern match strength (0.0-0.3): Strength of pattern recognition

## 5. API Design

### 5.1 Drawing Parser API

```python
def parse_drawing(file_path: str, format: str = "auto") -> DrawingData:
    """
    Parse engineering drawing and extract dimensions.
    
    Args:
        file_path: Path to drawing file
        format: File format (pdf, dxf, auto)
        
    Returns:
        DrawingData containing extracted dimensions
        
    Raises:
        UnsupportedFormatError: If format not supported
        CorruptedFileError: If file cannot be parsed
    """
```

### 5.2 Tolerance Chain API

```python
def identify_tolerance_chains(
    drawing_data: DrawingData,
    start_feature: str,
    end_feature: str
) -> List[ToleranceChain]:
    """
    Identify all tolerance chains between two features.
    
    Args:
        drawing_data: Parsed drawing data
        start_feature: Starting feature identifier
        end_feature: Ending feature identifier
        
    Returns:
        List of identified tolerance chains
    """
```

### 5.3 Stack-Up Calculator API

```python
def calculate_stackup(
    chain: ToleranceChain,
    method: CalculationMethod = CalculationMethod.BOTH
) -> StackUpResult:
    """
    Calculate tolerance stack-up for a chain.
    
    Args:
        chain: Tolerance chain to analyze
        method: WORST_CASE, STATISTICAL, or BOTH
        
    Returns:
        Stack-up calculation results
    """
```

### 5.4 Engineering Intent API

```python
def infer_engineering_intent(
    drawing_data: DrawingData
) -> EngineeringIntent:
    """
    Infer engineering intent from drawing.
    
    Args:
        drawing_data: Parsed drawing data
        
    Returns:
        EngineeringIntent with critical dimensions and patterns
    """
```

## 6. Technology Stack

### 6.1 Core Technologies

- **Language**: Python 3.9+
- **PDF Processing**: PyMuPDF (fitz) or pdfplumber
- **DXF Processing**: ezdxf
- **Graph Analysis**: NetworkX
- **Numerical Computing**: NumPy, SciPy
- **Data Validation**: Pydantic

### 6.2 Optional Technologies

- **OCR**: Tesseract (for scanned drawings)
- **Machine Learning**: scikit-learn (for pattern recognition)
- **Visualization**: Matplotlib (for chain visualization)
- **API Framework**: FastAPI (for REST API)

## 7. Implementation Phases

### Phase 1: Core Parsing
- Implement PDF/DXF parsers
- Implement dimension extraction
- Basic tolerance parsing

### Phase 2: Chain Analysis
- Implement feature graph builder
- Implement chain identification
- Direction analysis

### Phase 3: Stack-Up Calculation
- Implement worst-case calculator
- Implement statistical calculator
- Tolerance type handling

### Phase 4: Intent Inference
- Implement tolerance analyzer
- Implement fit analyzer
- Pattern recognition

## 8. Testing Strategy

### 8.1 Unit Tests
- Test each parser independently
- Test calculation algorithms with known inputs
- Test graph traversal algorithms

### 8.2 Integration Tests
- Test end-to-end workflows
- Test with sample engineering drawings
- Validate against manual calculations

### 8.3 Test Data
- Simple single-part drawings
- Multi-part assemblies
- Drawings with GD&T
- Edge cases (missing data, ambiguous dimensions)

## 9. Performance Considerations

- Cache parsed drawing data
- Optimize graph traversal for large assemblies
- Parallel processing for multiple chain analysis
- Lazy loading of drawing graphics

## 10. Security Considerations

- Validate file inputs to prevent malicious files
- Sanitize extracted text data
- Limit file size for uploads
- Implement rate limiting for API endpoints

## 11. Future Enhancements

- 3D CAD file support (STEP, IGES)
- Interactive visualization of tolerance chains
- Automated tolerance optimization suggestions
- Integration with PLM systems
- Machine learning for improved pattern recognition
- Support for international standards (ISO, ASME)
