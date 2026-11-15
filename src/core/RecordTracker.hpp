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
    bool selfDamage = false;
};

// Legacy offline inference result. Never displayed as an actual move record.
struct InferredRecord {
    bool valid = false;
    int actionId = -1;
    int card = -1;
    int damage = 0;
    double confidence = 0.0;
};

// Which card the BEST HIT slot should render. Kept as a pure function so the
// priority rules are unit-testable instead of being buried in Win32 paint code.
enum class RecordSource { Empty, Verified, Unattributed };

inline RecordSource pickRecordSource(const ActualRecord &best,
                                    const InferredRecord &,
                                    int, bool anyDamage) {
    // Damage similarity is not evidence of a cast's identity. Guesses are
    // never allowed onto the actual-record card, even when no cast is known.
    if(best.attributed && best.damage>0)return RecordSource::Verified;
    if(anyDamage)return RecordSource::Unattributed;
    return RecordSource::Empty;
}

// Single-cast damage records. The runtime supplies observed damage and a
// source instance from native creation events, so delayed/multi-hit projectiles keep
// their casting action. observe() retains the older HP-only API for callers
// without source snapshots; it cannot attribute delayed projectiles.
class RecordTracker {
public:
    void reset();
    void nextRound();
    void observe(int beforeHP, int afterHP, int attackAction, unsigned frame);
    // mixedSources: the frame's delta may include multiple independent moves.
    // Do not rank that aggregate as a single move, even as UNKNOWN MOVE.
    void observeDamage(int damage, int action, unsigned frame, std::uint64_t instance,
                       bool mixedSources = false, int damageCeiling = 0, bool selfDamage = false);
    // Damage already accumulated for one cast instance (0 when unknown).
    int castDamage(std::uint64_t instance) const;
    // Legacy diagnostics only; combo totals never establish cast identity.
    // The game's own combo counter is a damage observation that
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
    int unattributedPeak() const { return unattributedPeak_; }
    // Legacy combo diagnostics. Never used as a single-move display value.
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
    ActualRecord archivedBest_{}; // previous rounds, detached from reusable IDs
    std::uint64_t bestInstance_ = 0; // lets a rejected cast retract its record
    ActualRecord active_{};
    unsigned lastFrame_ = 0;
    unsigned lastHitFrame_ = 0;
    int unattributedDamage_ = 0;
    int unattributedPeak_ = 0;
    int comboPeak_ = 0;
    InferredRecord inferred_{};
};
}
