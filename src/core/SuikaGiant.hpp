#pragma once
// 1.10a Suika, Great Oni "Missing Purple Power" (MPP).
// During active giant form the command specials are unavailable. The only
// "highest usable move" candidates are giant-form ordinary actions.
//
// Duration field in SokuLib::CharacterManager: +0x892, 0..480 frames.
// Reference: SokuDev/SokuLib src/CharacterManager.hpp (pinned 0794bec).
// Single-contact damage: https://wikiwiki.jp/suika/【非想天則】ミッシングパープルパワー　詳細
// This deliberately does NOT invent full-combo sums from the repeated C
// burst (up to 8 shots) or 2A/B/C shockwave (up to 8 hits).
#include <array>
#include <cmath>

namespace whm {

struct SuikaGiantMove {
    const char *notation;
    int neutralDamage;
    bool multihitIncomplete; // Neutral figure only covers first known hit.
};

// Listed moves are the MPP form's available basic attacks, NOT Suika's
// normal FrameData character=9 attacks, which become inaccessible in MPP.
// Regular A/B/JA/JB shown here are *special shooting* despite their visual
// punches. Do not assume counter-hit melee modifiers based on artwork.
inline constexpr std::array<SuikaGiantMove, 10> kSuikaGiantMoves{{
    {"MPP WALK", 500, false},  // Walk impact, every other step
    {"MPP A", 2000, false},
    {"MPP B", 2000, false},
    {"MPP C", 2000, true},     // 2000 uppercut + repeated 500 blast shots
    {"MPP 2A/B/C", 500, true}, // 500 per shockwave hit, up to 8
    {"MPP HORN", 1500, false},
    {"MPP JUMP", 1800, false},
    {"MPP JA", 2500, false},
    {"MPP JB", 2500, false},
    {"MPP JC", 2000, false},
}};

constexpr bool suikaMPPActive(int character, unsigned timerFrames) noexcept {
    return character == 9 && timerFrames >= 1 && timerFrames <= 480;
}

inline int suikaGiantEstimatedHit(int neutralDamage, double effectiveMultiplier) {
    if (neutralDamage <= 0 || !std::isfinite(effectiveMultiplier) ||
        effectiveMultiplier <= 0. || effectiveMultiplier > 50.) return 0;
    return static_cast<int>(neutralDamage * effectiveMultiplier);
}

struct SuikaGiantBest {
    const SuikaGiantMove *move = nullptr;
    int damage = 0;
};
inline SuikaGiantBest strongestSuikaGiantMove(double effectiveMultiplier) {
    SuikaGiantBest best;
    for (const auto &move : kSuikaGiantMoves) {
        const int amount = suikaGiantEstimatedHit(move.neutralDamage, effectiveMultiplier);
        if (amount > best.damage) best = {&move, amount};
    }
    return best;
}
} // namespace whm
