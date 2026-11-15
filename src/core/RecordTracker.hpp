#pragma once
#include <cstdint>
#include <string>
#include <map>
namespace whm {

struct ActualRecord {
    int damage = 0;
    int actionId = -1;
    bool attributed = false;
    bool isSpell = false;
    // Set when this cast instance exceeded its move's theoretical ceiling:
    // its identity is not trustworthy, so later hits stay unranked too.
    bool rejected = false;
};

// A move reconstructed by simulating the ported damage formula against the
// game's own combo total. It is NOT a verified single-cast record: the HUD
// must mark it approximate and show only THIS cast's share of the combo.
struct InferredRecord {
    bool valid = false;
    int actionId = -1;
    int card = -1;
    int damage = 0;
    double confidence = 0.0;
};

// Which card the BEST HIT slot should render. Kept as a pure function so the
// priority rules are unit-testable instead of being buried in Win32 paint code.
enum class RecordSource { Empty, Verified, Inferred, Combo };
constexpr double kInferenceDisplayFloor = 0.35;   // minimum confidence to show at all
constexpr double kInferenceOutrankFloor = 0.60;   // minimum confidence to beat a real record

inline RecordSource pickRecordSource(const ActualRecord &best,
                                    const InferredRecord &inferred,
                                    int comboPeak, bool anyDamage) {
    const int verified = best.attributed ? best.damage : 0;
    const bool usable = inferred.valid &&
        inferred.confidence >= kInferenceDisplayFloor && inferred.damage > 0;
    // A DELAYED spell card (Marisa's 4-card SC) detonates long after its
    // caster left the animation, so HitAttribution reports mixed/orphan and
    // the 4000 damage never reaches best_. A high-confidence reverse-solved
    // cast must then be allowed to take the card -- otherwise the panel keeps
    // showing an older, much smaller verified hit forever. Three guards keep
    // this honest: high confidence, strictly larger than the verified record,
    // and never larger than the game's own observed combo peak (a bigger
    // number is a fitting artefact, not a hit). best_ is NEVER overwritten.
    if (usable && inferred.confidence >= kInferenceOutrankFloor &&
        inferred.damage > verified &&
        (comboPeak <= 0 || inferred.damage <= comboPeak))
        return RecordSource::Inferred;
    if (verified > 0) return RecordSource::Verified;
    if (usable) return RecordSource::Inferred; // no verified record yet
    if (anyDamage) return RecordSource::Combo;
    return RecordSource::Empty;
}

// Single-cast damage records. The runtime supplies observed damage and a
// source instance from HitAttribution, so delayed/multi-hit projectiles keep
// their casting action. observe() retains the older HP-only API for callers
// without source snapshots; it cannot attribute delayed projectiles.
class RecordTracker {
public:
    void reset();
    void observe(int beforeHP, int afterHP, int attackAction, unsigned frame);
    // mixedSources: the frame's delta may include multiple independent moves.
    // Do not rank that aggregate as a single move, even as UNKNOWN MOVE.
    void observeDamage(int damage, int action, unsigned frame, std::uint64_t instance,
                       bool mixedSources = false, int damageCeiling = 0);
    // Damage already accumulated for one cast instance (0 when unknown).
    int castDamage(std::uint64_t instance) const;
    // The game's own combo counter is an AUTHORITATIVE damage observation that
    // does not depend on whether we can guess which cast caused it. Feed it on
    // every engine update so the panel can prove that damage really happened
    // even when every frame of a sequence is unresolvable.
    void observeCombo(int comboDamage);
    // Keep the strongest simulated explanation of a mixed/unresolved combo.
    // Never writes into best_, so the verified-record invariant still holds.
    void observeInferred(int action, int card, int damage, double confidence);
    const InferredRecord &inferred() const { return inferred_; }
    void finish();
    // INVARIANT: the BEST HIT card is a ranking of verified single casts only.
    // Unresolvable/mixed damage is observed into unattributedDamage() but is
    // NEVER returned here. Showing an unattributed total as "UNKNOWN MOVE"
    // (or as a blank card that gets filled with a fabricated fallback) is
    // exactly the 5C+j6A regression captured in DamageTrace.log.
    const ActualRecord &best() const { return best_; }
    int unattributedDamage() const { return unattributedDamage_; }
    // Peak of the game's own combo damage this match. Used ONLY as a fallback
    // display value when best() is empty; it is a COMBO total, never a move.
    int comboPeak() const { return comboPeak_; }
    // True as soon as ANY damage was observed, regardless of attribution.
    // This is what makes "NO HIT RECORDED" impossible after a real hit.
    bool hasObservedDamage() const {
        return best_.damage>0||unattributedDamage_>0||comboPeak_>0;
    }
private:
    void commit();
    std::map<std::uint64_t,ActualRecord> casts_;
    ActualRecord best_{}; // only ever assigned attributed single-cast records
    std::uint64_t bestInstance_ = 0; // lets a rejected cast retract its record
    ActualRecord active_{};
    unsigned lastFrame_ = 0;
    unsigned lastHitFrame_ = 0;
    int unattributedDamage_ = 0;
    int comboPeak_ = 0;
    InferredRecord inferred_{};
};
}
