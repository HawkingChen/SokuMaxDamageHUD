#pragma once
#include "ActionIdentity.hpp"
#include <cstdint>
#include <map>
#include <vector>
#include <utility>

namespace whm {
struct AttackOrigin {
    int action = -1;
    std::uint64_t instance = 0;
    bool valid() const { return action >= 300 && action < 690 && instance; }
    bool operator==(const AttackOrigin &) const = default;
};
// A frame's total HP/combo delta cannot be split into move damage when
// independently spawned projectiles and body attacks may both contribute.
// Keep that total *unranked* rather than inventing a single-cast record.
struct HitAttributionResult {
    AttackOrigin origin{};
    bool mixed = false;
    // Diagnostic-only evidence counts. A collider cannot prove the whole
    // frame belonged to it when a different delayed projectile is live.
    unsigned detectedContacts = 0;
    unsigned plausibleOrigins = 0;
    unsigned liveProjectiles = 0;
    unsigned unownedOrigins = 0;
};
struct ActorContact {
    int action = 0, sequence = 0;
    unsigned animationFrame = 0;
    int collision = 0, limit = 0, hitStop = 0;
    bool hitBoxes = false;
};
struct ProjectileContact {
    std::uintptr_t id = 0, parent = 0;
    unsigned generation = 0;
    int collision = 0, limit = 0, hitStop = 0;
    bool hitBoxes = false, owned = false;
};
// Read-only snapshots, taken on either side of the original engine update.
// Remember the casting action at birth; never relabel an old projectile when
// its owner walks, attacks again, or returns to idle. Handle generation avoids
// confusing a recycled game-object address with the previous projectile.
class HitAttribution {
    struct Tracked { ProjectileContact sample; AttackOrigin origin; std::uint64_t tick = 0; };
    std::map<std::uintptr_t,Tracked> objects_;
    // Some sustained spells create the next child on the frame where its
    // parent emitter vanishes. Keep a *short-lived parent tombstone* for
    // explicitly linked children; never assign an unrelated owner bullet
    // merely because the fighter was casting a spell seconds earlier.
    std::map<std::uintptr_t,Tracked> endedParents_;
    std::uint64_t tick_ = 0;
    static constexpr std::uint64_t kParentGraceFrames = 90;
    ActorContact previousActor_{};
    AttackOrigin actorOrigin_{};
    std::uint64_t serial_ = 0;
    static bool contact(int now,int before,int limit,int oldLimit,int stop,int oldStop) {
        return (now==1||now==2||now==7) &&
               (now!=before || limit<oldLimit || stop>oldStop);
    }
    AttackOrigin actor(const ActorContact &s) {
        const bool attack=s.action>=300&&s.action<690;
        const int canonical=canonicalAttackAction(s.action);
        const bool sameRaw=s.action==previousActor_.action;
        // Moving 609 -> 659 is a spell's scripted follow-up, not a new
        // cast, even if sequence/frame resets at the cinematic transition.
        // A *new* 609 cast after 659 must start a new instance.
        const bool restarted=sameRaw && !isSpellFollowupAction(s.action) &&
            ((s.sequence==previousActor_.sequence &&
              s.animationFrame<previousActor_.animationFrame) ||
             (s.sequence==0 && previousActor_.sequence>0));
        const bool reactivatedBase=isSpellActivationAction(s.action) &&
            isSpellFollowupAction(previousActor_.action) &&
            canonical==canonicalAttackAction(previousActor_.action);
        if(!attack)actorOrigin_={};
        else if(canonical!=canonicalAttackAction(previousActor_.action)||
                restarted||reactivatedBase||!actorOrigin_.valid())
            actorOrigin_={canonical,++serial_};
        previousActor_=s;
        return actorOrigin_;
    }
    AttackOrigin born(const ProjectileContact &s,AttackOrigin casting) const {
        if(s.parent) {
            const auto live=objects_.find(s.parent);
            if(live!=objects_.end()&&live->second.origin.valid())return live->second.origin;
            const auto ended=endedParents_.find(s.parent);
            if(ended!=endedParents_.end() &&
               tick_-ended->second.tick<=kParentGraceFrames &&
               ended->second.origin.valid())return ended->second.origin;
            // An unresolved parent cannot be replaced with whichever action
            // the owner happens to be executing when the child materializes.
            return {};
        }
        // A delayed 5C projectile may spawn AFTER Suika has changed to j6A.
        // A newly seen bullet while its owner executes an A/melee normal
        // cannot be credited to that normal merely because it was current.
        // Shooting normals (400+) and specials may legitimately create it.
        if(casting.action<400)return {};
        return casting;
    }
public:
    void reset() { objects_.clear();endedParents_.clear();previousActor_={};actorOrigin_={};serial_=0;tick_=0; }
    HitAttributionResult observeDetailed(const ActorContact &before,const ActorContact &after,
                                         const std::vector<ProjectileContact> &pre,
                                         const std::vector<ProjectileContact> &post) {
        ++tick_;
        for(auto it=endedParents_.begin();it!=endedParents_.end();) {
            if(tick_-it->second.tick>kParentGraceFrames)it=endedParents_.erase(it);
            else ++it;
        }
        const auto beforeOrigin=actor(before);
        for(const auto &s:pre) {
            if(!s.owned)continue;
            auto it=objects_.find(s.id);
            if(it==objects_.end()||it->second.sample.generation!=s.generation)
                objects_[s.id]={s,born(s,beforeOrigin),tick_};
            else {it->second.sample=s;it->second.tick=tick_;}
            endedParents_.erase(s.id); // address reused by a live object
        }
        const auto afterOrigin=actor(after);
        const auto casting=beforeOrigin.valid()?beforeOrigin:afterOrigin;
        std::vector<AttackOrigin> hits,possible,latent;
        bool unknownHit=false;
        bool unresolvedProjectile=false;
        unsigned liveProjectileCount=0,unresolvedCount=0;
        auto add=[&](std::vector<AttackOrigin> &v,AttackOrigin origin) {
            if(!origin.valid())return;
            for(auto old:v)if(old==origin)return;
            v.push_back(origin);
        };
        if(before.hitBoxes)add(possible,beforeOrigin);
        if(after.hitBoxes)add(possible,afterOrigin);
        if((before.hitBoxes||after.hitBoxes)&&contact(after.collision,before.collision,
             after.limit,before.limit,after.hitStop,before.hitStop)) {
            // A fresh activation right after the previous card's ALT_EFFECT
            // must NOT inherit its final frame's collision as the old cast.
            // Only in this exact 650+ -> 600+ same-card transition, use the
            // *new* activation instance; 600+ -> 650+ stays the old instance.
            const bool newActivation=isSpellFollowupAction(before.action) &&
                isSpellActivationAction(after.action) &&
                canonicalAttackAction(before.action)==after.action &&
                afterOrigin.valid()&&afterOrigin!=beforeOrigin;
            add(hits,newActivation?afterOrigin:casting);
        }
        // Keep ALL currently live known bullets as latent candidates, even
        // if their immediate hit-box is not up this precise frame. Delayed
        // 5C must not be ignored just because j6A is the only contact that
        // changed its collision flag.
        for(const auto &[id,t]:objects_) {
            if(!t.sample.owned)continue;
            ++liveProjectileCount;
            if(t.origin.valid())add(latent,t.origin);
            else {unresolvedProjectile=true;++unresolvedCount;}
            if(t.sample.hitBoxes&&t.sample.limit>0)add(possible,t.origin);
        }
        std::map<std::uintptr_t,Tracked> next;
        for(const auto &s:post) {
            if(!s.owned)continue;
            const auto it=objects_.find(s.id);
            const bool known=it!=objects_.end()&&it->second.sample.generation==s.generation;
            const AttackOrigin origin=known?it->second.origin:born(s,casting);
            const ProjectileContact prior=known?it->second.sample:ProjectileContact{};
            if(contact(s.collision,prior.collision,s.limit,prior.limit,s.hitStop,prior.hitStop)) {
                if(origin.valid())add(hits,origin);else unknownHit=true;
            }
            if(!known) {
                // New post-frame objects are also evidence of possible
                // overlapping damage, regardless of whether their first
                // collision bit has changed yet.
                ++liveProjectileCount;
                if(origin.valid())add(latent,origin);
                else {unresolvedProjectile=true;++unresolvedCount;}
            }
            if(s.hitBoxes&&s.limit>0)add(possible,origin);
            next[s.id]={s,origin,tick_};
            endedParents_.erase(s.id);
        }
        // Save only known emitters that *actually disappear*. A child can
        // still identify their cast from parentObject for a short interval
        // even after the parent was removed by the engine.
        for(const auto &[id,parent]:objects_) {
            if(next.find(id)==next.end()&&parent.origin.valid())
                endedParents_[id]={parent.sample,parent.origin,tick_};
        }
        objects_=std::move(next);
        HitAttributionResult decision;
        decision.detectedContacts=static_cast<unsigned>(hits.size());
        decision.plausibleOrigins=static_cast<unsigned>(possible.size());
        decision.liveProjectiles=liveProjectileCount;
        decision.unownedOrigins=unresolvedCount;
        if(unknownHit || hits.size()>1) {decision.mixed=true;return decision;}
        if(hits.size()==1) {
            // A sole detected body contact cannot own the whole HP decrease
            // if another live projectile might deal a delayed hit.
            // Also fail closed for new bullets whose source cannot be
            // established, rather than silently relabeling them as j6A.
            if(unresolvedProjectile){decision.mixed=true;return decision;}
            for(const auto &candidate:possible)
                if(candidate!=hits.front()){decision.mixed=true;return decision;}
            for(const auto &candidate:latent)
                if(candidate!=hits.front()){decision.mixed=true;return decision;}
            decision.origin=hits.front();return decision;
        }
        // Without a detected hit, only use an unambiguous, active hit box.
        // A latent object is insufficient evidence on its own.
        if(unresolvedProjectile && (possible.size()>0 || !latent.empty())) {
            decision.mixed=true;return decision;
        }
        if(possible.size()==1){decision.origin=possible.front();return decision;}
        if(!possible.empty()){decision.mixed=true;return decision;}
        // A scripted ALT_EFFECT attack can legitimately lack an ordinary
        // collision transition; preserve its own cast in that narrow case.
        if(isSpellFollowupAction(after.action) && afterOrigin.valid() &&
           afterOrigin.action==canonicalAttackAction(after.action)) {
            decision.origin=afterOrigin;return decision;
        }
        if(isSpellFollowupAction(before.action) && beforeOrigin.valid() &&
           beforeOrigin.action==canonicalAttackAction(before.action)) {
            decision.origin=beforeOrigin;return decision;
        }
        return decision;
    }
    AttackOrigin observe(const ActorContact &before,const ActorContact &after,
                         const std::vector<ProjectileContact> &pre,
                         const std::vector<ProjectileContact> &post) {
        return observeDetailed(before,after,pre,post).origin;
    }
};

struct ComboReading { int hits=0,damage=0; bool freshTarget=false; };
inline int observedDamage(int beforeHP,int afterHP,ComboReading before,ComboReading after) {
    // Prefer the actual HP decrease for this engine update. Combo damage is
    // a *cumulative* value, not a fresh per-action amount; on combo reset or
    // re-stand, assigning after.damage to the newest attack can re-credit
    // the previous projectile's entire combo (e.g. Suika 5C + j6A).
    const int hpDamage=beforeHP>afterHP?beforeHP-afterHP:0;
    if(hpDamage>0)return hpDamage;
    int comboDelta=0;
    if(after.hits>0 && after.damage>0 &&
       (after.hits!=before.hits || after.damage!=before.damage)) {
        if(before.hits==0)comboDelta=after.damage;
        else if(before.freshTarget && after.hits<=before.hits &&
                after.damage<before.damage)
            comboDelta=after.damage; // proven new combo after target recovered
        else if(!before.freshTarget &&
                after.hits>before.hits && after.damage>before.damage)
            comboDelta=after.damage-before.damage;
        // Existing combo counters that reset/decrease are not trustworthy
        // as a one-frame damage delta when HP hasn't changed.
    }
    // Practice may restore HP inside the original update: fall back to
    // a monotonic combo increment, preserving this earlier behavior.
    if(beforeHP>0 && comboDelta>beforeHP)comboDelta=beforeHP;
    return comboDelta;
}
} // namespace whm
