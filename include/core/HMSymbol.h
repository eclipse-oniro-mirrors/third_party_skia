/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef LIB_RS_SRC_HM_SYMBOL_H_
#define LIB_RS_SRC_HM_SYMBOL_H_
#include <iostream>
#include <vector>
#include <string>
#include <map>

#include "SkPath.h"

enum AnimationType {
    INVALID_ANIMATION_TYPE = 0,
    SCALE_TYPE = 1,
    VARIABLE_COLOR_TYPE = 2,
    APPEAR_TYPE = 3,
    DISAPPEAR_TYPE = 4,
    BOUNCE_TYPE = 5,
    PULSE_TYPE = 6,
    REPLACE_APPEAR_TYPE = 7,
    REPLACE_DISAPPEAR_TYPE = 8,
    DISABLE_TYPE = 9,
    QUICK_REPLACE_APPEAR_TYPE = 10,
    QUICK_REPLACE_DISAPPEAR_TYPE = 11,
};

enum CurveType {
    INVALID_CURVE_TYPE = 0,
    SPRING = 1,
    LINEAR = 2,
    FRICTION = 3,
    SHARP = 4,
};

enum CommonSubType {
    DOWN = 0,
    UP = 1,
};

using PiecewiseParameter = struct PiecewiseParameter {
    CurveType curveType = CurveType::INVALID_CURVE_TYPE;
    std::map<std::string, float> curveArgs;
    uint32_t duration = 0;
    int delay = 0;
    std::map<std::string, std::vector<float>> properties;
};

using AnimationPara = struct AnimationPara {
    uint16_t animationMode = 0; // 0 is default value, is byLayer effect
    CommonSubType commonSubType = CommonSubType::DOWN;
    std::vector<std::vector<PiecewiseParameter>> groupParameters;
};

using AnimationInfo = struct AnimationInfo {
    AnimationType animationType = AnimationType::INVALID_ANIMATION_TYPE;
    std::map<uint32_t, AnimationPara> animationParas;
};

using SColor = struct SColor {
    float a = 1;
    U8CPU r = 0;
    U8CPU g = 0;
    U8CPU b = 0;
};

using GroupInfo = struct GroupInfo {
    std::vector<size_t> layerIndexes;
    std::vector<size_t> maskIndexes;
};

using GroupSetting = struct GroupSetting {
    std::vector<GroupInfo> groupInfos;
    int animationIndex = -1; // -1 is default value, the level has no effecet
};

using AnimationSetting = struct AnimationSetting {
    std::vector<AnimationType> animationTypes;
    std::vector<GroupSetting> groupSettings;
    CommonSubType commonSubType = CommonSubType::DOWN;
    double slope = 0.0;
};

using RenderGroup = struct RenderGroup {
    std::vector<GroupInfo> groupInfos;
    SColor color;
};

enum EffectStrategy {
    NONE = 0,
    SCALE = 1,
    VARIABLE_COLOR = 2,
    APPEAR = 3,
    DISAPPEAR = 4,
    BOUNCE = 5,
    PULSE = 6,
    REPLACE_APPEAR = 7,
    REPLACE_DISAPPEAR = 8,
    QUICK_REPLACE_APPEAR = 10,
    QUICK_REPLACE_DISAPPEAR = 11,
};

using SymbolLayers = struct SymbolLayers {
    uint16_t symbolGlyphId = 0;
    std::vector<std::vector<size_t>> layers;
    std::vector<RenderGroup> renderGroups;
};

enum SymbolRenderingStrategy {
    SINGLE = 0,
    MULTIPLE_COLOR = 1,
    MULTIPLE_OPACITY = 2,
};


using SymbolLayersGroups = struct SymbolLayersGroups {
    uint16_t symbolGlyphId = 0;
    std::vector<std::vector<size_t>> layers;
    std::map<SymbolRenderingStrategy, std::vector<RenderGroup>> renderModeGroups;
    std::vector<AnimationSetting> animationSettings;
};

class SK_API HMSymbolData
{
public:
    SymbolLayers symbolInfo_;
    SkPath path_;
    uint64_t symbolId_ = 0;
};

class SK_API HMSymbol
{
public:
    HMSymbol(){};

    ~HMSymbol(){};

    static void PathOutlineDecompose(const SkPath& path, std::vector<SkPath>& paths);

    static void MultilayerPath(const std::vector<std::vector<size_t>>& multMap,
        const std::vector<SkPath>& paths, std::vector<SkPath>& multPaths);
};

#endif