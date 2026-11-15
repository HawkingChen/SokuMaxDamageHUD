#pragma once
#include <array>
#include <cstddef>
#include <istream>
#include <string>
#include <vector>

namespace whm {

// One candidate cast from Cleista's framedata summary.
// character|action|card|kind|notation|neutralTotal|perHitBase|hitCount|prorationPermille
struct MoveFingerprint {
    int character = -1;
    int action = -1;
    int card = -1;
    int kind = 0; // 0 normal, 1 skill, 2 spell
    std::string notation;
    int neutralTotal = 0;   // measured full-hit damage on a fresh 10k HP dummy
    int perHitBase = 0;     // damage of ONE hit (e.g. Suika 5C = 150)
    int hitCount = 1;       // hits in a full cast (e.g. Suika 5C = 18)
    int prorationPermille = 980;
    double calibration = 1.0; // computed once: neutralTotal / simulated full cast
};

// Everything the ported 0x463f50 scaler needs at combo start.
struct InferenceState {
    int character = -1;
    int defenderHP = 10000;
    int comboHits = 0;            // game's authoritative combo hit count
    double comboRate = 1.0;       // game's own +0x4B0 value, 1.0 at combo start
    double attack = 1.0, defense = 1.0;
    double skillMultiplier = 1.0, spellMultiplier = 1.0;
    int weather = 0;
    bool typeIncludesWeather = true;
    int actorAction = -1;                 // current body-hit hint
    std::vector<int> originActions;      // known projectile owners this combo
};

struct InferredPart {
    int action = -1;
    int card = -1;
    int damage = 0;
    int hits = 0;
};

// An explanation of an OBSERVED COMBO TOTAL, never of a single frame.
struct InferenceResult {
    bool valid = false;
    int target = 0;                 // authoritative combo damage we are explaining
    int expected = 0;               // damage of the chosen explanation
    int residual = 0;               // |target - expected|
    double confidence = 0.0;
    int primaryAction = -1;         // the cast contributing the most
    int primaryCard = -1;
    int primaryDamage = 0;          // THAT CAST'S share, never the whole combo
    std::vector<InferredPart> parts;
    std::string reason;
};

// A cast inside a combo can never have dealt more than the combo's own
// cumulative damage. When that total is at or below the verified BEST HIT,
// no inferred cast could raise the record, so the solver can be skipped.
inline bool inferenceCanBeatRecord(int comboDamage, int bestDamage) {
    return comboDamage > bestDamage;
}

class DamageInference {
public:
    // Reads config/HitProfiles.tsv. Returns false only on malformed rows.
    bool read(std::istream &in, std::vector<std::string> *errors = nullptr);
    std::size_t size() const { return moves_.size(); }
    const std::vector<MoveFingerprint> &forCharacter(int character) const;
    // Largest measured full-hit damage for an action (all variants included).
    // Used as a sanity ceiling: a cast cannot deal more than its own data.
    int maxNeutralDamage(int character, int action) const;

    // Simulate k hits of one cast with the ported scaler and the calibrated
    // per-hit base. The calibration makes a FULL cast reproduce neutralTotal.
    int expectedDamage(const MoveFingerprint &move, int hits,
                       const InferenceState &state,
                       double startRate, int startHP) const;

    // Reverse-solve a combo total into the most likely cast composition.
    // Single casts are preferred; pairs are tried only from live evidence
    // (projectile owners / current body action) plus the heaviest moves.
    InferenceResult solve(int observedComboDamage, const InferenceState &state) const;

private:
    struct Simulation { int damage = 0; double endRate = 1.0; int endHP = 0; };
    Simulation simulate(const MoveFingerprint &move, int hits,
                        const InferenceState &state,
                        double startRate, int startHP) const;
    double calibration(const MoveFingerprint &move) const;
    static double typeBonus(const MoveFingerprint &move, const InferenceState &state);
    static double weatherBonus(const MoveFingerprint &move, const InferenceState &state);
    static int truncMul(int value, double factor);
    std::vector<MoveFingerprint> moves_;
    std::array<std::vector<MoveFingerprint>, 20> byCharacter_{};
};

} // namespace whm
