#pragma once
// Meiling's Tiger Chi (623B/C) / Fierce Tiger Inner Strength (SC) is a
// melee-only, one-hit Counter Hit effect. The usual 4/3 CH and the Tiger
// multiplier apply to the FIRST qualifying melee hit, not every projectile
// or each hit of a multi-hit attack.
//
// Published mechanics: https://wikiwiki.jp/hi12/非想天則/システム関連
// Attack classification: https://w.atwiki.jp/bulletaction/pages/250.html
// Cleista's bundled FrameData.txt has total neutral damage, not per-hit
// damage. For moves with a confirmed single melee hit, that total is the
// first hit; for multi-hit/conditional moves do not invent per-hit values.
#include <algorithm>
#include <cmath>

namespace whm {

enum class MeilingFirstHitKind {
    Unknown,
    Projectile,
    SingleMelee,
    MixedOrMultipleMelee
};

// Skill card IDs are specifically for Meiling (character 17), NOT global IDs.
constexpr MeilingFirstHitKind meilingFirstHitKind(int character, int card) {
    if (character != 17) return MeilingFirstHitKind::Unknown;
    switch (card) {
        case 101: // 623B/C Red Cannon: the first direct punch suppresses projectile
        case 102: // 22B/C Yellow Quake Kick
        case 105: // 623B/C Red Energy Release
        case 108: // 214B/C Kick of Showering Brilliance
        case 110: // 22B/C Sky Dragon Kick
            return MeilingFirstHitKind::SingleMelee;
        case 103: // 236 Fragrant Wave
        case 106: // 22 Earth Dragon Wave
        case 107: // 236 Water Tai Chi Fist
        case 111: // 236 Colorful Rain
            return MeilingFirstHitKind::Projectile;
        case 100: // 214 Spiral Light Steps: extra stomp at higher levels
        case 104: // 214 Searing Red Fist: several direct blows + finish
            return MeilingFirstHitKind::MixedOrMultipleMelee;
        default:
            return MeilingFirstHitKind::Unknown;
    }
}

// GameObjectBase::counterHitDmgMultiplier (+0x198) supplies Tiger's extra
// CH scale: Tiger Chi Lv1=1; Lv2=1.1; Lv3=1.2; Lv4=1.3;
// Fierce Tiger=2. The normal forced CH is another 4/3 multiplier.
// Only use this with the independently verified active timer/forced-hit count.
// Do not multiply a game-maintained attackPower twice.
inline double meilingTigerFirstHitFactor(bool armed, double counterHitScale) {
    if (!armed || !std::isfinite(counterHitScale) ||
        counterHitScale < 1. || counterHitScale > 3.) return 1.;
    return (4. / 3.) * counterHitScale;
}

// Both neutralTotal and firstMeleeHitReference are sourced neutral damage,
// before current attack/defense multipliers. For multi-hit skills the latter
// MUST be the *first qualifying melee hit*, not the whole move. With unknown
// first-hit damage pass 0 to preserve the unboosted estimate instead.
// The output remains approximate because individual game hits round separately.
inline int meilingTigerAdjustedDamage(int character, int card,
                                     int neutralTotal, double effectiveFactor,
                                     bool armed, double counterHitScale,
                                     int firstMeleeHitReference = 0) {
    if (neutralTotal <= 0 || !std::isfinite(effectiveFactor) ||
        effectiveFactor <= 0. || effectiveFactor > 50.) return 0;
    const double ordinary = std::floor(neutralTotal * effectiveFactor);
    const auto kind = meilingFirstHitKind(character, card);
    const double tiger = meilingTigerFirstHitFactor(armed, counterHitScale);
    if (tiger <= 1.) return static_cast<int>(ordinary);
    int firstHit = 0;
    if (kind == MeilingFirstHitKind::SingleMelee)
        firstHit = neutralTotal;
    else if (kind == MeilingFirstHitKind::MixedOrMultipleMelee)
        firstHit = std::clamp(firstMeleeHitReference, 0, neutralTotal);
    // First-hit-only CH and Tiger correction. Extra rounding needs live test.
    return static_cast<int>(ordinary) +
           static_cast<int>(std::floor(firstHit * effectiveFactor * (tiger - 1.) + 1e-8));
}
} // namespace whm
