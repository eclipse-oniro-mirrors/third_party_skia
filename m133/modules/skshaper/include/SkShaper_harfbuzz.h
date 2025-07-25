/*
 * Copyright 2024 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkShaper_harfbuzz_DEFINED
#define SkShaper_harfbuzz_DEFINED

#include "include/core/SkFourByteTag.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "modules/skshaper/include/SkShaper.h"

#include <cstddef>
#include <memory>

class SkFontMgr;
class SkUnicode;
#ifdef ENABLE_DRAWING_ADAPTER
namespace SkiaRsText {
#endif
namespace SkShapers::HB {
#ifdef ENABLE_TEXT_ENHANCE
SKSHAPER_API std::unique_ptr<SkShaper> ShaperDrivenWrapper(sk_sp<SkUnicode> unicode,
                                                           std::shared_ptr<RSFontMgr> fallback);
SKSHAPER_API std::unique_ptr<SkShaper> ShapeThenWrap(sk_sp<SkUnicode> unicode,
                                                     std::shared_ptr<RSFontMgr> fallback);
SKSHAPER_API std::unique_ptr<SkShaper> ShapeDontWrapOrReorder(sk_sp<SkUnicode> unicode,
                                                              std::shared_ptr<RSFontMgr> fallback);
#else
SKSHAPER_API std::unique_ptr<SkShaper> ShaperDrivenWrapper(sk_sp<SkUnicode> unicode,
                                                           sk_sp<SkFontMgr> fallback);
SKSHAPER_API std::unique_ptr<SkShaper> ShapeThenWrap(sk_sp<SkUnicode> unicode,
                                                     sk_sp<SkFontMgr> fallback);
SKSHAPER_API std::unique_ptr<SkShaper> ShapeDontWrapOrReorder(sk_sp<SkUnicode> unicode,
                                                              sk_sp<SkFontMgr> fallback);
#endif
SKSHAPER_API std::unique_ptr<SkShaper::ScriptRunIterator> ScriptRunIterator(const char* utf8,
                                                                            size_t utf8Bytes);
SKSHAPER_API std::unique_ptr<SkShaper::ScriptRunIterator> ScriptRunIterator(const char* utf8,
                                                                            size_t utf8Bytes,
                                                                            SkFourByteTag script);

SKSHAPER_API void PurgeCaches();
} // namespace SkShapers::HB
#ifdef ENABLE_DRAWING_ADAPTER
} // namespace SkiaRsText
namespace SkShapers::HB{
    using namespace SkiaRsText::SkShapers::HB;
}
#endif
#endif
