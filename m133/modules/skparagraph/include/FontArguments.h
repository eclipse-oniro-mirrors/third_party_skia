// Copyright 2019 Google LLC.
#ifndef FontArguments_DEFINED
#define FontArguments_DEFINED

#include <functional>
#include <vector>
#include "include/core/SkFontArguments.h"
#include "include/core/SkTypeface.h"
#ifdef ENABLE_DRAWING_ADAPTER
#include "drawing.h"
#endif

namespace skia {
namespace textlayout {

class FontArguments {
public:
    FontArguments(const SkFontArguments&);
    FontArguments(const FontArguments&) = default;
    FontArguments(FontArguments&&) = default;

    FontArguments& operator=(const FontArguments&) = default;
    FontArguments& operator=(FontArguments&&) = default;

#ifndef ENABLE_DRAWING_ADAPTER
    sk_sp<SkTypeface> CloneTypeface(const sk_sp<SkTypeface>& typeface) const;
#else
    std::shared_ptr<RSTypeface> CloneTypeface(std::shared_ptr<RSTypeface> typeface) const;
#endif

    friend bool operator==(const FontArguments& a, const FontArguments& b);
    friend bool operator!=(const FontArguments& a, const FontArguments& b);
    friend struct std::hash<FontArguments>;

private:
    FontArguments() = delete;

    int fCollectionIndex;
    std::vector<SkFontArguments::VariationPosition::Coordinate> fCoordinates;
    int fPaletteIndex;
    std::vector<SkFontArguments::Palette::Override> fPaletteOverrides;
};

}  // namespace textlayout
}  // namespace skia

namespace std {
    template<> struct hash<skia::textlayout::FontArguments> {
        size_t operator()(const skia::textlayout::FontArguments& args) const;
    };
}

#endif  // FontArguments_DEFINED
