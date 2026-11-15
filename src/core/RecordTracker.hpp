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
};

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
                       bool mixedSources = false);
    void finish();
    // INVARIANT: the BEST HIT card is a ranking of verified single casts only.
    // Unresolvable/mixed damage is observed into unattributedDamage() but is
    // NEVER returned here. Showing an unattributed total as "UNKNOWN MOVE"
    // (or as a blank card that gets filled with a fabricated fallback) is
    // exactly the 5C+j6A regression captured in DamageTrace.log. If nothing
    // has been verified, best().damage is 0 and the panel shows NO RECORD.
    const ActualRecord &best() const { return best_; }
    int unattributedDamage() const { return unattributedDamage_; }
private:
    void commit();
    std::map<std::uint64_t,ActualRecord> casts_;
    ActualRecord best_{}; // only ever assigned attributed single-cast records
    ActualRecord active_{};
    unsigned lastFrame_ = 0;
    unsigned lastHitFrame_ = 0;
    int unattributedDamage_ = 0;
};
}
