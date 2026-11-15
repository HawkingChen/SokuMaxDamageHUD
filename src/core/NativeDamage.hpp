#pragma once
#include "ActionIdentity.hpp"
#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace whm {
// Values copied at the engine event, before objects can disappear/reuse memory.
struct NativeEvent {
    enum Kind { Hit, Birth, Action } kind=Hit;
    unsigned frame=0,generation=0,creatorGeneration=0,parentGeneration=0;
    std::uintptr_t object=0,victim=0,owner=0,ally=0,creator=0,parent=0,caller=0;
    int action=0,sequence=0,boxAction=0,ownerAction=0,victimAction=0;
    int damage=0,hp=0,ownerHP=0,comboHits=0,comboDamageBefore=0,protectedHP=0;
    int path=0,castAction=-1;
    std::uint64_t cast=0;
};

struct NativeCast {
    int action=-1;
    std::uint64_t id=0;
    bool valid() const { return id && action>=300 && action<690; }
};

// Engine action writes delimit casts. A spell's ALT_EFFECT continuation is
// the same cast; returning to its activation action starts a NEW cast.
class NativeActors {
    struct Actor { int raw=-1; NativeCast cast; } actors_[2];
    std::uint64_t serial_=0;
public:
    void reset() { for(auto &a:actors_)a={}; } // serial never reused
    NativeCast get(unsigned side) const { return side<2?actors_[side].cast:NativeCast{}; }
    NativeCast set(unsigned side,int action,int sequence=-1) {
        if(side>=2)return {};
        auto &a=actors_[side];
        const bool continuation=isSpellFollowupAction(action) && a.cast.valid() &&
            a.cast.action==canonicalAttackAction(action);
        const bool subsequence=sequence>0 && action==a.raw && a.cast.valid();
        a.raw=action;
        if(action<300||action>=690)a.cast={};
        else if(!continuation&&!subsequence)a.cast={canonicalAttackAction(action),++serial_};
        return a.cast;
    }
};

struct NativeDamage {
    int damage=0,action=-1;
    std::uint64_t instance=0;
    bool self=false;
};

class NativeAttribution {
    using ObjectKey=std::pair<std::uintptr_t,unsigned>;
    struct Object {
        std::uintptr_t owner=0;
        NativeCast root;
        ObjectKey creator{};
    };
    std::map<ObjectKey,Object> objects_;
    // One spell damaging both players must not sum the two victims together.
    std::map<std::pair<std::uint64_t,std::uintptr_t>,std::uint64_t> records_;
    std::uint64_t serial_=0;

    NativeCast resolve(ObjectKey key,std::uintptr_t owner) const {
        // Cycles or incomplete graphs stay unknown. No frame/time guesses.
        for(unsigned depth=0;depth<128;++depth) {
            const auto it=objects_.find(key);
            if(it==objects_.end()||it->second.owner!=owner)return {};
            const auto &o=it->second;
            if(o.root.valid())return o.root;
            if(!o.creator.first || o.creator==key)return {};
            key=o.creator;
        }
        return {};
    }
public:
    void reset() { objects_.clear();records_.clear(); } // serial never reused
    std::vector<NativeDamage> consume(std::span<const NativeEvent> batch,
                                      std::uintptr_t local,bool truncated=false) {
        // Factory returns can be child-before-parent. Build every birth first.
        for(const auto &e:batch)if(e.kind==NativeEvent::Birth) {
            Object o;o.owner=e.owner;
            if(e.creator==e.owner && e.cast)o.root={e.castAction,e.cast};
            else if(e.creator)o.creator={e.creator,e.creatorGeneration};
            else if(e.parent)o.creator={e.parent,e.parentGeneration};
            objects_[{e.object,e.generation}]=o;
        }
        std::vector<NativeDamage> result;
        for(const auto &e:batch) {
            if(e.kind!=NativeEvent::Hit || e.damage<=0)continue;
            // ally is the engine's combo credit, owner is the physical owner.
            if(e.ally!=local && e.owner!=local)continue;
            NativeCast source;
            if(!truncated && e.owner==local && e.ally==local) {
                if(e.object==e.owner) {
                    if(e.cast && canonicalAttackAction(e.boxAction)==e.castAction)
                        source={e.castAction,e.cast};
                } else source=resolve({e.object,e.generation},e.owner);
            }
            std::uint64_t instance=0;
            if(source.valid()) {
                const auto key=std::make_pair(source.id,e.victim);
                auto [it,inserted]=records_.try_emplace(key,0);
                if(inserted)it->second=++serial_;
                instance=it->second;
            }
            result.push_back({e.damage,source.valid()?source.action:-1,instance,e.victim==local});
        }
        return result;
    }
};
}
