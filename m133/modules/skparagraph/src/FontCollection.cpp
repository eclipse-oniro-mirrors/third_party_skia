// Copyright 2019 Google LLC.
#include "modules/skparagraph/include/FontCollection.h"

#include "include/core/SkTypeface.h"
#include "modules/skparagraph/include/Paragraph.h"
#include "modules/skparagraph/src/ParagraphImpl.h"
#include "modules/skshaper/include/SkShaper_harfbuzz.h"

namespace {
#ifndef ENABLE_DRAWING_ADAPTER
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
    const char* kColorEmojiFontMac = "Apple Color Emoji";
#else
    const char* kColorEmojiLocale = "und-Zsye";
#endif
#endif // ENABLE_DRAWING_ADAPTER

#ifdef ENABLE_DRAWING_ADAPTER
std::shared_ptr<RSTypeface> RSLegacyMakeTypeface(
    std::shared_ptr<RSFontMgr> fontMgr, const char familyName[], RSFontStyle style)
{
    RSTypeface* typeface = fontMgr->MatchFamilyStyle(familyName, style);
    if (typeface == nullptr && familyName != nullptr) {
        typeface = fontMgr->MatchFamilyStyle(nullptr, style);
    }

    if (typeface) {
        return std::shared_ptr<RSTypeface>(typeface);
    }
    return nullptr;
}
#endif

constexpr int MAX_VARTYPEFACE_SIZE = 32;
#ifdef ENABLE_TEXT_ENHANCE
std::unordered_map<uint32_t, std::shared_ptr<RSTypeface>> g_faceTypeCache(MAX_VARTYPEFACE_SIZE);
#endif
}
namespace skia {
namespace textlayout {
#ifdef ENABLE_TEXT_ENHANCE
bool FontCollection::fIsAdpaterTextHeightEnabled = false;
#endif
bool FontCollection::FamilyKey::operator==(const FontCollection::FamilyKey& other) const {
    return fFamilyNames == other.fFamilyNames &&
           fFontStyle == other.fFontStyle &&
           fFontArguments == other.fFontArguments;
}

size_t FontCollection::FamilyKey::Hasher::operator()(const FontCollection::FamilyKey& key) const {
    size_t hash = 0;
    for (const SkString& family : key.fFamilyNames) {
        hash ^= std::hash<std::string>()(family.c_str());
    }
#ifdef ENABLE_DRAWING_ADAPTER
    return hash ^
           std::hash<uint32_t>()(key.fFontStyle.GetWeight()) ^
           std::hash<uint32_t>()(static_cast<uint32_t>(key.fFontStyle.GetSlant())) ^
           std::hash<std::optional<FontArguments>>()(key.fFontArguments);
#else
    return hash ^
           std::hash<uint32_t>()(key.fFontStyle.weight()) ^
           std::hash<uint32_t>()(key.fFontStyle.slant()) ^
           std::hash<std::optional<FontArguments>>()(key.fFontArguments);
#endif
}

FontCollection::FontCollection()
    : fEnableFontFallback(true),
    fDefaultFamilyNames({SkString(DEFAULT_FONT_FAMILY)}) {}

size_t FontCollection::getFontManagersCount() const {
    std::shared_lock<std::shared_mutex> readLock(mutex_);
    return this->getFontManagerOrder().size();
}

#ifdef ENABLE_DRAWING_ADAPTER
void FontCollection::setAssetFontManager(std::shared_ptr<RSFontMgr> font_manager) {
    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    fAssetFontManager = font_manager;
}
#else
void FontCollection::setAssetFontManager(sk_sp<SkFontMgr> font_manager) {
    fAssetFontManager = std::move(font_manager);
}
#endif

#ifdef ENABLE_DRAWING_ADAPTER
void FontCollection::setDynamicFontManager(std::shared_ptr<RSFontMgr> font_manager) {
    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    fDynamicFontManager = font_manager;
}
#else
void FontCollection::setDynamicFontManager(sk_sp<SkFontMgr> font_manager) {
    fDynamicFontManager = std::move(font_manager);
}
#endif

#ifdef ENABLE_DRAWING_ADAPTER
void FontCollection::setTestFontManager(std::shared_ptr<RSFontMgr> font_manager)
{
    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    fTestFontManager = font_manager;
}
#else
void FontCollection::setTestFontManager(sk_sp<SkFontMgr> font_manager) {
    fTestFontManager = std::move(font_manager);
}
#endif

#ifdef ENABLE_DRAWING_ADAPTER
void FontCollection::setDefaultFontManager(std::shared_ptr<RSFontMgr> fontManager,
    const char defaultFamilyName[]) {
    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    fDefaultFontManager = std::move(fontManager);
    fDefaultFamilyNames.emplace_back(defaultFamilyName);
}
#else
void FontCollection::setDefaultFontManager(sk_sp<SkFontMgr> fontManager,
    const char defaultFamilyName[]) {
    fDefaultFontManager = std::move(fontManager);
    fDefaultFamilyNames.emplace_back(defaultFamilyName);
}
#endif

#ifdef ENABLE_DRAWING_ADAPTER
void FontCollection::setDefaultFontManager(std::shared_ptr<RSFontMgr> fontManager,
                                           const std::vector<SkString>& defaultFamilyNames) {
    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    fDefaultFontManager = std::move(fontManager);
    fDefaultFamilyNames = defaultFamilyNames;
}
#else
void FontCollection::setDefaultFontManager(sk_sp<SkFontMgr> fontManager,
                                           const std::vector<SkString>& defaultFamilyNames) {
    fDefaultFontManager = std::move(fontManager);
    fDefaultFamilyNames = defaultFamilyNames;
}
#endif

#ifdef ENABLE_DRAWING_ADAPTER
void FontCollection::setDefaultFontManager(std::shared_ptr<RSFontMgr> fontManager) {
    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    fDefaultFontManager = fontManager;
}
#else
void FontCollection::setDefaultFontManager(sk_sp<SkFontMgr> fontManager) {
    fDefaultFontManager = std::move(fontManager);
}
#endif

// Return the available font managers in the order they should be queried.
#ifdef ENABLE_DRAWING_ADAPTER
std::vector<std::shared_ptr<RSFontMgr>> FontCollection::getFontManagerOrder() const {
    std::vector<std::shared_ptr<RSFontMgr>> order;
#else
std::vector<sk_sp<SkFontMgr>> FontCollection::getFontManagerOrder() const {
    std::vector<sk_sp<SkFontMgr>> order;
#endif
    if (fDynamicFontManager) {
        order.push_back(fDynamicFontManager);
    }
    if (fAssetFontManager) {
        order.push_back(fAssetFontManager);
    }
    if (fTestFontManager) {
        order.push_back(fTestFontManager);
    }
    if (fDefaultFontManager && fEnableFontFallback) {
        order.push_back(fDefaultFontManager);
    }
    return order;
}

#ifdef ENABLE_DRAWING_ADAPTER
std::vector<std::shared_ptr<RSTypeface>> FontCollection::findTypefaces(
    const std::vector<SkString>& familyNames, RSFontStyle fontStyle)
{
#else
std::vector<sk_sp<SkTypeface>> FontCollection::findTypefaces(
    const std::vector<SkString>& familyNames, SkFontStyle fontStyle) 
{
#endif
    return findTypefaces(familyNames, fontStyle, std::nullopt);
}

#ifdef ENABLE_DRAWING_ADAPTER
std::vector<std::shared_ptr<RSTypeface>> FontCollection::findTypefaces(const std::vector<SkString>& familyNames,
    RSFontStyle fontStyle, const std::optional<FontArguments>& fontArgs)
{
    // Look inside the font collections cache first
    FamilyKey familyKey(familyNames, fontStyle, fontArgs);
    {
        std::shared_lock<std::shared_mutex> readLock(mutex_);
        auto found = fTypefaces.find(familyKey);
        if (found != fTypefaces.end()) {
            return found->second;
        }
    }

    std::vector<std::shared_ptr<RSTypeface>> typefaces;
    for (const auto& familyName : familyNames) {
        std::shared_ptr<RSTypeface> match = matchTypeface(familyName, fontStyle);
        if (match) {
            match = CloneTypeface(match, fontArgs);
            typefaces.emplace_back(std::move(match));
        }
    }

    if (typefaces.empty()) {
        std::shared_ptr<RSTypeface> match;
        for (const auto& familyName : fDefaultFamilyNames) {
            match = matchTypeface(familyName, fontStyle);
            if (match) {
                match = CloneTypeface(match, fontArgs);
                typefaces.emplace_back(std::move(match));
            }
        }

        if (typefaces.empty()) {
            for (const auto& manager : this->getFontManagerOrder()) {
                match = RSLegacyMakeTypeface(manager, nullptr, fontStyle);
                if (match) {
                    typefaces.emplace_back(std::move(match));
                    break;
                }
            }
        }
    }

    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    fTypefaces.emplace(familyKey, typefaces);
    return typefaces;
}
#else
std::vector<sk_sp<SkTypeface>> FontCollection::findTypefaces(const std::vector<SkString>& familyNames,
    SkFontStyle fontStyle, const std::optional<FontArguments>& fontArgs) {
    // Look inside the font collections cache first
    FamilyKey familyKey(familyNames, fontStyle, fontArgs);
    auto found = fTypefaces.find(familyKey);
    if (found) {
        return *found;
    }

    std::vector<sk_sp<SkTypeface>> typefaces;
    for (const SkString& familyName : familyNames) {
        sk_sp<SkTypeface> match = matchTypeface(familyName, fontStyle);
        if (match && fontArgs) {
            match = fontArgs->CloneTypeface(match);
        }
        if (match) {
            typefaces.emplace_back(std::move(match));
        }
    }

    if (typefaces.empty()) {
        sk_sp<SkTypeface> match;
        for (const SkString& familyName : fDefaultFamilyNames) {
            match = matchTypeface(familyName, fontStyle);
            if (match) {
                break;
            }
        }
        if (!match) {
            for (const auto& manager : this->getFontManagerOrder()) {
                match = manager->legacyMakeTypeface(nullptr, fontStyle);
                if (match) {
                    break;
                }
            }
        }
        if (match) {
            typefaces.emplace_back(std::move(match));
        }
    }

    fTypefaces.set(familyKey, typefaces);
    return typefaces;
}
#endif

#ifdef ENABLE_DRAWING_ADAPTER
std::shared_ptr<RSTypeface> FontCollection::matchTypeface(const SkString& familyName, RSFontStyle fontStyle) {
    for (const auto& manager : this->getFontManagerOrder()) {
        std::shared_ptr<RSFontStyleSet> set(manager->MatchFamily(familyName.c_str()));
        if (!set || set->Count() == 0) {
            continue;
        }

        std::shared_ptr<RSTypeface> match(set->MatchStyle(fontStyle));
        if (match) {
            return match;
        }
    }

    return nullptr;
}
#else
sk_sp<SkTypeface> FontCollection::matchTypeface(const SkString& familyName, SkFontStyle fontStyle) {
    for (const auto& manager : this->getFontManagerOrder()) {
        sk_sp<SkFontStyleSet> set(manager->matchFamily(familyName.c_str()));
        if (!set || set->count() == 0) {
            continue;
        }

        sk_sp<SkTypeface> match(set->matchStyle(fontStyle));
        if (match) {
            return match;
        }
    }

    return nullptr;
}
#endif

// Find ANY font in available font managers that resolves the unicode codepoint
#ifdef ENABLE_DRAWING_ADAPTER
std::shared_ptr<RSTypeface> FontCollection::defaultFallback(
    SkUnichar unicode, RSFontStyle fontStyle, const SkString& locale)
{
    std::shared_lock<std::shared_mutex> readLock(mutex_);
    for (const auto& manager : this->getFontManagerOrder()) {
        std::vector<const char*> bcp47;
        if (!locale.isEmpty()) {
            bcp47.push_back(locale.c_str());
        }
        std::shared_ptr<RSTypeface> typeface(manager->MatchFamilyStyleCharacter(
                nullptr, fontStyle, bcp47.data(), bcp47.size(), unicode));
        if (typeface != nullptr) {
            return typeface;
        }
    }
    return nullptr;
}
#else
sk_sp<SkTypeface> FontCollection::defaultFallback(SkUnichar unicode,
                                                  SkFontStyle fontStyle,
                                                  const SkString& locale) {

    for (const auto& manager : this->getFontManagerOrder()) {
        std::vector<const char*> bcp47;
        if (!locale.isEmpty()) {
            bcp47.push_back(locale.c_str());
        }
        sk_sp<SkTypeface> typeface(manager->matchFamilyStyleCharacter(
            nullptr, fontStyle, bcp47.data(), bcp47.size(), unicode));

        if (typeface != nullptr) {
            return typeface;
        }
    }
    return nullptr;
}
#endif


#ifndef ENABLE_DRAWING_ADAPTER
// Find ANY font in available font managers that resolves this emojiStart
sk_sp<SkTypeface> FontCollection::defaultEmojiFallback(SkUnichar emojiStart,
                                                       SkFontStyle fontStyle,
                                                       const SkString& locale) {

    for (const auto& manager : this->getFontManagerOrder()) {
        std::vector<const char*> bcp47;
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
        sk_sp<SkTypeface> emojiTypeface =
            fDefaultFontManager->matchFamilyStyle(kColorEmojiFontMac, SkFontStyle());
        if (emojiTypeface != nullptr) {
            return emojiTypeface;
        }
#else
          bcp47.push_back(kColorEmojiLocale);
#endif
        if (!locale.isEmpty()) {
            bcp47.push_back(locale.c_str());
        }

        // Not really ideal since the first codepoint may not be the best one
        // but we start from a good colored emoji at least
        sk_sp<SkTypeface> typeface(manager->matchFamilyStyleCharacter(
            nullptr, fontStyle, bcp47.data(), bcp47.size(), emojiStart));
        if (typeface != nullptr) {
            // ... and stop as soon as we find something in hope it will work for all of them
            return typeface;
        }
    }
    return nullptr;
}
#endif

#ifdef ENABLE_DRAWING_ADAPTER
std::shared_ptr<RSTypeface> FontCollection::defaultFallback() {
    std::shared_lock<std::shared_mutex> readLock(mutex_);
    if (fDefaultFontManager == nullptr) {
        return nullptr;
    }
    for (const auto& familyName : fDefaultFamilyNames) {
        std::shared_ptr<RSTypeface> match = std::shared_ptr<RSTypeface>(
            fDefaultFontManager->MatchFamilyStyle(familyName.c_str(), RSFontStyle()));
        if (match) {
            return match;
        }
    }
    return nullptr;
}
#else
sk_sp<SkTypeface> FontCollection::defaultFallback() {
    if (fDefaultFontManager == nullptr) {
        return nullptr;
    }
    for (const SkString& familyName : fDefaultFamilyNames) {
        sk_sp<SkTypeface> match = fDefaultFontManager->matchFamilyStyle(familyName.c_str(),
                                                                        SkFontStyle());
        if (match) {
            return match;
        }
    }
    return nullptr;
}
#endif

#ifdef ENABLE_DRAWING_ADAPTER
class SkLRUCacheMgr {
public:
    SkLRUCacheMgr(SkLRUCache<uint32_t, std::shared_ptr<RSTypeface>>& lruCache, SkMutex& mutex)
        :fLRUCache(lruCache), fMutex(mutex)
    {
        fMutex.acquire();
    }
    SkLRUCacheMgr(const SkLRUCacheMgr&) = delete;
    SkLRUCacheMgr(SkLRUCacheMgr&&) = delete;
    SkLRUCacheMgr& operator=(const SkLRUCacheMgr&) = delete;
    SkLRUCacheMgr& operator=(SkLRUCacheMgr&&) = delete;

    ~SkLRUCacheMgr() {
        fMutex.release();
    }

    std::shared_ptr<RSTypeface> find(uint32_t fontId) {
        auto face = fLRUCache.find(fontId);
        return face == nullptr ? nullptr : *face;
    }

    std::shared_ptr<RSTypeface> insert(uint32_t fontId, std::shared_ptr<RSTypeface> hbFont) {
        auto face = fLRUCache.insert(fontId, std::move(hbFont));
        return face == nullptr ? nullptr : *face;
    }

    void reset() {
        fLRUCache.reset();
    }

private:
    SkLRUCache<uint32_t, std::shared_ptr<RSTypeface>>& fLRUCache;
    SkMutex& fMutex;
};

static SkLRUCacheMgr GetLRUCacheInstance() {
    static SkMutex gFaceCacheMutex;
    static SkLRUCache<uint32_t, std::shared_ptr<RSTypeface>> gFaceCache(MAX_VARTYPEFACE_SIZE);
    return SkLRUCacheMgr(gFaceCache, gFaceCacheMutex);
}
#endif

#ifdef ENABLE_DRAWING_ADAPTER
std::shared_ptr<RSTypeface> FontCollection::CloneTypeface(std::shared_ptr<RSTypeface> typeface,
    const std::optional<FontArguments>& fontArgs)
{
#else
sk_sp<SkTypeface> FontCollection::CloneTypeface(sk_sp<SkTypeface> typeface,
    const std::optional<FontArguments>& fontArgs)
{
#endif
#ifdef ENABLE_DRAWING_ADAPTER
    if (!typeface || !fontArgs || typeface->IsCustomTypeface()) {
#else
    if (!typeface || !fontArgs || typeface->isCustomTypeface()) {
#endif
        return typeface;
    }

    size_t hash = 0;
    hash ^= std::hash<FontArguments>()(fontArgs.value());
#ifdef ENABLE_DRAWING_ADAPTER
    hash ^= std::hash<uint32_t>()(typeface->GetUniqueID());
#else
    hash ^= std::hash<uint32_t>()(typeface->uniqueID());
#endif

    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    auto cached = GetLRUCacheInstance().find(hash);
    if (cached) {
        return cached;
    } else {
        auto varTypeface = fontArgs->CloneTypeface(typeface);
        if (!varTypeface) {
            return typeface;
        }
        GetLRUCacheInstance().insert(hash, varTypeface);
        return varTypeface;
    }
}


void FontCollection::disableFontFallback() {
    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    fEnableFontFallback = false;
}

void FontCollection::enableFontFallback() {
    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    fEnableFontFallback = true;
}

void FontCollection::clearCaches() {
    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    fParagraphCache.reset();
#ifdef ENABLE_DRAWING_ADAPTER
    fTypefaces.clear();
#else
    fTypefaces.reset();
#endif
    SkShapers::HB::PurgeCaches();
}

}  // namespace textlayout
}  // namespace skia
