#include "RecordTracker.hpp"
#include <algorithm>
namespace whm {
void RecordTracker::reset() { casts_.clear(); best_ = {}; bestInstance_ = 0; active_ = {}; lastFrame_ = 0; lastHitFrame_ = 0; unattributedDamage_ = 0; comboPeak_ = 0; inferred_ = {}; }
void RecordTracker::observeInferred(int action,int card,int damage,double confidence) {
    if(damage<=0||action<300||action>=690)return;
    if(damage<inferred_.damage)return; // keep the strongest explanation
    inferred_={true,action,card,damage,confidence};
}
void RecordTracker::observeCombo(int comboDamage) {
    // comboDamage is the game's cumulative combo damage. It is authoritative
    // even when HitAttribution cannot identify a cast. Keep the match peak;
    // a new round resets the counter but not the observed peak, matching the
    // existing "keep best across rounds" policy.
    if(comboDamage>comboPeak_)comboPeak_=comboDamage;
}
void RecordTracker::commit() {
    // Defensive guard for the legacy HP-only API: an unattributed episode can
    // never become the BEST HIT, even if it carries a larger HP delta.
    if (active_.attributed && active_.damage > best_.damage) best_ = active_;
    active_ = {};
}
void RecordTracker::finish() { commit(); }
void RecordTracker::observeDamage(int damage,int action,unsigned frame,std::uint64_t instance,
                                  bool mixedSources,int damageCeiling) {
    if(frame<lastFrame_)casts_.clear(); // new round, preserve match best
    lastFrame_=frame;
    if(damage<=0)return;
    if(mixedSources) {
        // The source cannot be divided into separate moves, but the damage
        // really happened. Keep it as observed-but-unranked damage: never
        // claim it was all j6A, and never surface it as an "UNKNOWN MOVE"
        // record. DamageTrace.log shows these frames are 5C+j6A combos
        // (2280/777, 1956/206/795/65), not single moves.
        unattributedDamage_+=damage;
        return;
    }
    if(action<300||action>=690||!instance) {
        // An unresolvable frame is still counted, but it is not a move.
        unattributedDamage_+=damage;
        return;
    }
    auto &record=casts_[instance];
    if(record.rejected){unattributedDamage_+=damage;return;}
    if(record.damage && record.actionId!=action)return; // invalid source reuse
    // A cast cannot deal more than its own move's measured full-hit damage.
    // The caller supplies 1.5x the Cleista neutralTotal as the ceiling, which
    // still allows weather / attack / counter-hit multipliers. DamageTrace
    // (2026-11-15 12:20): a spell's 2000+1500 was credited to j.5C (414,
    // max 1107) and became "j.5C 3500" because casts_[instance] had no bound.
    // Reject the identity instead of ranking an impossible move, and retract
    // any record this instance already wrote.
    if(damageCeiling>0 && record.damage+damage>damageCeiling) {
        record.rejected=true;
        record.damage=0;
        if(bestInstance_==instance){best_={};bestInstance_=0;}
        unattributedDamage_+=damage;
        return;
    }
    record.actionId=action;record.attributed=true;record.isSpell=action>=600;
    record.damage+=damage;
    if(record.damage>best_.damage){best_=record;bestInstance_=instance;}
}
int RecordTracker::castDamage(std::uint64_t instance) const {
    const auto it=casts_.find(instance);
    return it==casts_.end()||it->second.rejected?0:it->second.damage;
}
void RecordTracker::observe(int beforeHP, int afterHP, int action, unsigned frame) {
    // Frame counter may restart on the next ROUND. Preserve the match record.
    if (frame < lastFrame_) commit();
    const int delta = std::max(0, beforeHP - afterHP);
    // A contiguous action episode: only count hits when action is valid and is
    // unmistakably an attack animation. This is not sufficient for projectiles.
    const bool attributed = (action >= 300 && action < 690);
    if (active_.damage > 0 && (action != active_.actionId || frame - lastHitFrame_ > 15 || !attributed)) commit();
    if (delta > 0) {
        if (!attributed) { unattributedDamage_ += delta; }
        else {
            active_.attributed = true;
            active_.actionId = action;
            active_.isSpell = action >= 600;
            active_.damage += delta;
            lastHitFrame_ = frame;
            if (active_.damage > best_.damage) best_ = active_; // live preview
        }
    }
    lastFrame_ = frame;
}
}
