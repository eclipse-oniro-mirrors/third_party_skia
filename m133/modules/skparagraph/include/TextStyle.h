// Copyright 2019 Google LLC.
#ifndef TextStyle_DEFINED
#define TextStyle_DEFINED

#include <optional>
#include <vector>
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/core/SkScalar.h"
#include "modules/skparagraph/include/DartTypes.h"
#include "modules/skparagraph/include/FontArguments.h"
#include "modules/skparagraph/include/ParagraphPainter.h"
#include "modules/skparagraph/include/TextShadow.h"
#ifdef ENABLE_TEXT_ENHANCE
#include "drawing.h"
#endif // ENABLE_TEXT_ENHANCE
// TODO: Make it external so the other platforms (Android) could use it
#define DEFAULT_FONT_FAMILY "sans-serif"

namespace skia {
namespace textlayout {
#ifdef ENABLE_TEXT_ENHANCE
const SkScalar TEXT_BADGE_FONT_SIZE_SCALE = 0.65;
const SkScalar SUPERSCRIPT_BASELINE_SHIFT_SCALE = -0.7;
const SkScalar SUBSCRIPT_BASELINE_SHIFT_SCALE = 0.2;
#endif

static inline bool nearlyZero(SkScalar x, SkScalar tolerance = SK_ScalarNearlyZero) {
    if (SkIsFinite(x)) {
        return SkScalarNearlyZero(x, tolerance);
    }
    return false;
}

static inline bool nearlyEqual(SkScalar x, SkScalar y, SkScalar tolerance = SK_ScalarNearlyZero) {
#ifdef ENABLE_TEXT_ENHANCE
    if (SkIsNaN(x) && SkIsNaN(y)) {
        // Generally NaN has no equality, but it will break the invariant of the hashtable
        // in ParagraphCache, resulting in errors. This fix is only a backstop for
        // this condition, other functions may still be unreliable in the presence of NaN.
        return true;
    }
#endif
    if (SkIsFinite(x, y)) {
        return SkScalarNearlyEqual(x, y, tolerance);
    }
    // Inf == Inf, anything else is false
    return x == y;
}

// Multiple decorations can be applied at once. Ex: Underline and overline is
// (0x1 | 0x2)
enum TextDecoration {
    kNoDecoration = 0x0,
    kUnderline = 0x1,
    kOverline = 0x2,
    kLineThrough = 0x4,
};
constexpr TextDecoration AllTextDecorations[] = {
        kNoDecoration,
        kUnderline,
        kOverline,
        kLineThrough,
};

enum TextDecorationStyle { kSolid, kDouble, kDotted, kDashed, kWavy };

enum TextDecorationMode { kGaps, kThrough };

enum StyleType {
    kNone,
    kAllAttributes,
    kFont,
    kForeground,
    kBackground,
    kShadow,
    kDecorations,
    kLetterSpacing,
    kWordSpacing
};

struct Decoration {
    TextDecoration fType;
    TextDecorationMode fMode;
    SkColor fColor;
    TextDecorationStyle fStyle;
    SkScalar fThicknessMultiplier;

    bool operator==(const Decoration& other) const {
        return this->fType == other.fType &&
               this->fMode == other.fMode &&
               this->fColor == other.fColor &&
               this->fStyle == other.fStyle &&
               this->fThicknessMultiplier == other.fThicknessMultiplier;
    }
};

/// Where to vertically align the placeholder relative to the surrounding text.
enum class PlaceholderAlignment {
  /// Match the baseline of the placeholder with the baseline.
  kBaseline,

  /// Align the bottom edge of the placeholder with the baseline such that the
  /// placeholder sits on top of the baseline.
  kAboveBaseline,

  /// Align the top edge of the placeholder with the baseline specified in
  /// such that the placeholder hangs below the baseline.
  kBelowBaseline,

  /// Align the top edge of the placeholder with the top edge of the font.
  /// When the placeholder is very tall, the extra space will hang from
  /// the top and extend through the bottom of the line.
  kTop,

  /// Align the bottom edge of the placeholder with the top edge of the font.
  /// When the placeholder is very tall, the extra space will rise from
  /// the bottom and extend through the top of the line.
  kBottom,

  /// Align the middle of the placeholder with the middle of the text. When the
  /// placeholder is very tall, the extra space will grow equally from
  /// the top and bottom of the line.
  kMiddle,

#ifdef ENABLE_TEXT_ENHANCE
  // Follow text vertically aligned
  kFollow,
#endif
};

struct FontFeature {
    FontFeature(SkString name, int value) : fName(std::move(name)), fValue(value) {}
    bool operator==(const FontFeature& that) const {
        return fName == that.fName && fValue == that.fValue;
    }
    SkString fName;
    int fValue;
};

struct PlaceholderStyle {
    PlaceholderStyle() = default;
    PlaceholderStyle(SkScalar width, SkScalar height, PlaceholderAlignment alignment,
                     TextBaseline baseline, SkScalar offset)
            : fWidth(width)
            , fHeight(height)
            , fAlignment(alignment)
            , fBaseline(baseline)
            , fBaselineOffset(offset) {}

    bool equals(const PlaceholderStyle&) const;

    SkScalar fWidth = 0;
    SkScalar fHeight = 0;
    PlaceholderAlignment fAlignment = PlaceholderAlignment::kBaseline;
    TextBaseline fBaseline = TextBaseline::kAlphabetic;
    // Distance from the top edge of the rect to the baseline position. This
    // baseline will be aligned against the alphabetic baseline of the surrounding
    // text.
    //
    // Positive values drop the baseline lower (positions the rect higher) and
    // small or negative values will cause the rect to be positioned underneath
    // the line. When baseline == height, the bottom edge of the rect will rest on
    // the alphabetic baseline.
    SkScalar fBaselineOffset = 0;
};

#ifdef ENABLE_TEXT_ENHANCE
struct RectStyle {
    SkColor color{0};
    SkScalar leftTopRadius{0.0f};
    SkScalar rightTopRadius{0.0f};
    SkScalar rightBottomRadius{0.0f};
    SkScalar leftBottomRadius{0.0f};

    bool operator ==(const RectStyle& rhs) const {
        return color == rhs.color &&
            leftTopRadius == rhs.leftTopRadius &&
            rightTopRadius == rhs.rightTopRadius &&
            rightBottomRadius == rhs.rightBottomRadius &&
            leftBottomRadius == rhs.leftBottomRadius;
    }

    bool operator !=(const RectStyle& rhs) const {
        return !(color == rhs.color &&
            leftTopRadius == rhs.leftTopRadius &&
            rightTopRadius == rhs.rightTopRadius &&
            rightBottomRadius == rhs.rightBottomRadius &&
            leftBottomRadius == rhs.leftBottomRadius);
    }
};

enum class TextBadgeType {
    BADGE_NONE,
    SUPERSCRIPT,
    SUBSCRIPT,
};
#endif

class TextStyle {
public:
    TextStyle() = default;
    TextStyle(const TextStyle& other) = default;
    TextStyle& operator=(const TextStyle& other) = default;

    TextStyle cloneForPlaceholder();

    bool equals(const TextStyle& other) const;
    bool equalsByFonts(const TextStyle& that) const;
    bool matchOneAttribute(StyleType styleType, const TextStyle& other) const;
    bool operator==(const TextStyle& rhs) const { return this->equals(rhs); }

    // Colors
    SkColor getColor() const { return fColor; }
    void setColor(SkColor color) { fColor = color; }

    bool hasForeground() const { return fHasForeground; }
    SkPaint getForeground() const {
        const SkPaint* paint = std::get_if<SkPaint>(&fForeground);
        return paint ? *paint : SkPaint();
    }
    ParagraphPainter::SkPaintOrID getForegroundPaintOrID() const {
        return fForeground;
    }
    void setForegroundPaint(SkPaint paint) {
        fHasForeground = true;
        fForeground = std::move(paint);
    }
    // DEPRECATED: prefer `setForegroundPaint`.
    void setForegroundColor(SkPaint paint) { setForegroundPaint(std::move(paint)); }

    // Set the foreground to a paint ID.  This is intended for use by clients
    // that implement a custom ParagraphPainter that can not accept an SkPaint.
    void setForegroundPaintID(ParagraphPainter::PaintID paintID) {
        fHasForeground = true;
        fForeground = paintID;
    }
    void clearForegroundColor() { fHasForeground = false; }

    bool hasBackground() const { return fHasBackground; }
    SkPaint getBackground() const {
        const SkPaint* paint = std::get_if<SkPaint>(&fBackground);
        return paint ? *paint : SkPaint();
    }
    ParagraphPainter::SkPaintOrID getBackgroundPaintOrID() const {
        return fBackground;
    }
    void setBackgroundPaint(SkPaint paint) {
        fHasBackground = true;
        fBackground = std::move(paint);
    }
    // DEPRECATED: prefer `setBackgroundPaint`.
    void setBackgroundColor(SkPaint paint) { setBackgroundPaint(std::move(paint)); }
    void setBackgroundPaintID(ParagraphPainter::PaintID paintID) {
        fHasBackground = true;
        fBackground = paintID;
    }
    void clearBackgroundColor() { fHasBackground = false; }

    // Decorations
    Decoration getDecoration() const { return fDecoration; }
    TextDecoration getDecorationType() const { return fDecoration.fType; }
    TextDecorationMode getDecorationMode() const { return fDecoration.fMode; }
    SkColor getDecorationColor() const { return fDecoration.fColor; }
    TextDecorationStyle getDecorationStyle() const { return fDecoration.fStyle; }
    SkScalar getDecorationThicknessMultiplier() const {
        return fDecoration.fThicknessMultiplier;
    }
    void setDecoration(TextDecoration decoration) { fDecoration.fType = decoration; }
    void setDecorationMode(TextDecorationMode mode) { fDecoration.fMode = mode; }
    void setDecorationStyle(TextDecorationStyle style) { fDecoration.fStyle = style; }
    void setDecorationColor(SkColor color) { fDecoration.fColor = color; }
    void setDecorationThicknessMultiplier(SkScalar m) { fDecoration.fThicknessMultiplier = m; }

    // Weight/Width/Slant
#ifdef ENABLE_TEXT_ENHANCE
    RSFontStyle getFontStyle() const { return fFontStyle; }
    void setFontStyle(RSFontStyle fontStyle) { fFontStyle = fontStyle; }
#else
    SkFontStyle getFontStyle() const { return fFontStyle; }
    void setFontStyle(SkFontStyle fontStyle) { fFontStyle = fontStyle; }
#endif

    // Shadows
    size_t getShadowNumber() const { return fTextShadows.size(); }
    std::vector<TextShadow> getShadows() const { return fTextShadows; }
    void addShadow(TextShadow shadow) { fTextShadows.emplace_back(shadow); }
    void resetShadows() { fTextShadows.clear(); }

    // Font features
    size_t getFontFeatureNumber() const { return fFontFeatures.size(); }
    std::vector<FontFeature> getFontFeatures() const { return fFontFeatures; }
    void addFontFeature(const SkString& fontFeature, int value)
        { fFontFeatures.emplace_back(fontFeature, value); }
    void resetFontFeatures() { fFontFeatures.clear(); }

    // Font arguments
    const std::optional<FontArguments>& getFontArguments() const { return fFontArguments; }
    // The contents of the SkFontArguments will be copied into the TextStyle,
    // and the SkFontArguments can be safely deleted after setFontArguments returns.
    void setFontArguments(const std::optional<SkFontArguments>& args);

    SkScalar getFontSize() const { return fFontSize; }
    void setFontSize(SkScalar size) { fFontSize = size; }

    const std::vector<SkString>& getFontFamilies() const { return fFontFamilies; }

#ifdef ENABLE_TEXT_ENHANCE
    void setFontFamilies(std::vector<SkString> families);

    SkScalar getBaselineShift() const { return fBaselineShift; }
    SkScalar getVerticalAlignShift() const { return fVerticalAlignShift; };
    SkScalar getTotalVerticalShift() const { return fBaselineShift + getBadgeBaseLineShift(); }
    void setVerticalAlignShift(SkScalar shift) { fVerticalAlignShift = shift; }
#else
    void setFontFamilies(std::vector<SkString> families) {
        fFontFamilies = std::move(families);
    }

    SkScalar getBaselineShift() const { return fBaselineShift; }
#endif

    void setBaselineShift(SkScalar baselineShift) { fBaselineShift = baselineShift; }

    void setHeight(SkScalar height) { fHeight = height; }
    SkScalar getHeight() const { return fHeightOverride ? fHeight : 0; }

    void setHeightOverride(bool heightOverride) { fHeightOverride = heightOverride; }
    bool getHeightOverride() const { return fHeightOverride; }

    void setHalfLeading(bool halfLeading) { fHalfLeading = halfLeading; }
    bool getHalfLeading() const { return fHalfLeading; }

    void setLetterSpacing(SkScalar letterSpacing) { fLetterSpacing = letterSpacing; }
    SkScalar getLetterSpacing() const { return fLetterSpacing; }

    void setWordSpacing(SkScalar wordSpacing) { fWordSpacing = wordSpacing; }
    SkScalar getWordSpacing() const { return fWordSpacing; }

#ifdef ENABLE_TEXT_ENHANCE
    RSTypeface* getTypeface() const { return fTypeface.get(); }
    std::shared_ptr<RSTypeface> refTypeface() const { return fTypeface; }
    void setTypeface(std::shared_ptr<RSTypeface> typeface) { fTypeface = std::move(typeface); }
#else
    SkTypeface* getTypeface() const { return fTypeface.get(); }
    sk_sp<SkTypeface> refTypeface() const { return fTypeface; }
    void setTypeface(sk_sp<SkTypeface> typeface) { fTypeface = std::move(typeface); }
#endif

    SkString getLocale() const { return fLocale; }
    void setLocale(const SkString& locale) { fLocale = locale; }

    TextBaseline getTextBaseline() const { return fTextBaseline; }
    void setTextBaseline(TextBaseline baseline) { fTextBaseline = baseline; }

#ifdef ENABLE_TEXT_ENHANCE
    void getFontMetrics(RSFontMetrics* metrics) const;
#else
    void getFontMetrics(SkFontMetrics* metrics) const;
#endif

    bool isPlaceholder() const { return fIsPlaceholder; }
    void setPlaceholder() { fIsPlaceholder = true; }

#ifdef ENABLE_TEXT_ENHANCE
    int getStyleId() const { return fStyleId; }
    void setStyleId(int styleId) { fStyleId = static_cast<SkColor>(styleId); }
    size_t getTextStyleUid() const { return fTextStyleUid; }
    void setTextStyleUid(size_t textStyleUid) { fTextStyleUid = textStyleUid; }
    RectStyle getBackgroundRect() const { return fBackgroundRect; }
    void setBackgroundRect(RectStyle rect) { fBackgroundRect = rect; }
    bool isCustomSymbol() const { return fIsCustomSymbol; }

    void setCustomSymbol(bool state) { fIsCustomSymbol = state; }

    TextBadgeType getTextBadgeType() const { return fBadgeType; }

    void setTextBadgeType(TextBadgeType badgeType) { fBadgeType = badgeType; }

    SkScalar getBadgeBaseLineShift() const;

    SkScalar getCorrectFontSize() const;
#endif
private:
    static const std::vector<SkString>* kDefaultFontFamilies;

    Decoration fDecoration = {
            TextDecoration::kNoDecoration,
#ifdef ENABLE_TEXT_ENHANCE
            TextDecorationMode::kGaps,
#else
            // TODO: switch back to kGaps when (if) switching flutter to skparagraph
            TextDecorationMode::kThrough,
#endif
            // It does not make sense to draw a transparent object, so we use this as a default
            // value to indicate no decoration color was set.
            SK_ColorTRANSPARENT, TextDecorationStyle::kSolid,
            // Thickness is applied as a multiplier to the default thickness of the font.
            1.0f};

#ifdef ENABLE_TEXT_ENHANCE
    RSFontStyle fFontStyle;
#else
    SkFontStyle fFontStyle;
#endif

    std::vector<SkString> fFontFamilies = *kDefaultFontFamilies;

    SkScalar fFontSize = 14.0;
    SkScalar fHeight = 1.0;
    bool fHeightOverride = false;
    SkScalar fBaselineShift = 0.0f;
    // true: half leading.
    // false: scale ascent/descent with fHeight.
    bool fHalfLeading = false;
    SkString fLocale = {};
    SkScalar fLetterSpacing = 0.0;
    SkScalar fWordSpacing = 0.0;
#ifdef ENABLE_TEXT_ENHANCE
    RectStyle fBackgroundRect = {0, 0.0f, 0.0f, 0.0f, 0.0f};
    SkColor fStyleId = {0};
    size_t fTextStyleUid{0};
    SkScalar fVerticalAlignShift{0.0f};
#endif

    TextBaseline fTextBaseline = TextBaseline::kAlphabetic;

    SkColor fColor = SK_ColorWHITE;
    bool fHasBackground = false;
    ParagraphPainter::SkPaintOrID fBackground;
    bool fHasForeground = false;
    ParagraphPainter::SkPaintOrID fForeground;

    std::vector<TextShadow> fTextShadows;

#ifdef ENABLE_TEXT_ENHANCE
    bool fIsCustomSymbol{false};
    std::shared_ptr<RSTypeface> fTypeface;
#else
    sk_sp<SkTypeface> fTypeface;
#endif
    bool fIsPlaceholder = false;

    std::vector<FontFeature> fFontFeatures;

    std::optional<FontArguments> fFontArguments;

#ifdef ENABLE_TEXT_ENHANCE
    TextBadgeType fBadgeType{TextBadgeType::BADGE_NONE};
#endif
};

typedef size_t TextIndex;
typedef SkRange<size_t> TextRange;
const SkRange<size_t> EMPTY_TEXT = EMPTY_RANGE;

struct Block {
    Block() = default;
    Block(size_t start, size_t end, const TextStyle& style) : fRange(start, end), fStyle(style) {}
    Block(TextRange textRange, const TextStyle& style) : fRange(textRange), fStyle(style) {}

    void add(TextRange tail) {
        SkASSERT(fRange.end == tail.start);
        fRange = TextRange(fRange.start, fRange.start + fRange.width() + tail.width());
    }

    TextRange fRange = EMPTY_RANGE;
    TextStyle fStyle;
};


typedef size_t BlockIndex;
typedef SkRange<size_t> BlockRange;
const size_t EMPTY_BLOCK = EMPTY_INDEX;
const SkRange<size_t> EMPTY_BLOCKS = EMPTY_RANGE;

struct Placeholder {
    Placeholder() = default;
    Placeholder(size_t start, size_t end, const PlaceholderStyle& style, const TextStyle& textStyle,
                BlockRange blocksBefore, TextRange textBefore)
            : fRange(start, end)
            , fStyle(style)
            , fTextStyle(textStyle)
            , fBlocksBefore(blocksBefore)
            , fTextBefore(textBefore) {}

    TextRange fRange = EMPTY_RANGE;
    PlaceholderStyle fStyle;
    TextStyle fTextStyle;
    BlockRange fBlocksBefore;
    TextRange fTextBefore;
};

}  // namespace textlayout
}  // namespace skia

#endif  // TextStyle_DEFINED
