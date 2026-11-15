#include "NativeRecording.hpp"
#include "NativeDamage.hpp"
#include "DamageEngine.hpp"
#include "ActionIdentity.hpp"
#include "MoveCatalog.hpp"
#include "WikiMax.hpp"
#include "RecordTracker.hpp"
#include "LocalPlayerSide.hpp"
#include "HudPalette.hpp"
#include "HudText.hpp"
#include <cfenv>
#include "FirstHit.hpp"
#include "MeilingTiger.hpp"
#include "SuikaGiant.hpp"
#include "PanelLayout.hpp"
#include "HitAttribution.hpp"
#include "SkillSelection.hpp"
#include "OpeningCounter.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <vector>
#include <utility>
#include "DamageInference.hpp"

static void testProjectileRecords() {
    using namespace whm;
    HitAttribution attribution;
    ActorContact cast;cast.action=561;cast.animationFrame=10;
    ActorContact idle;
    ProjectileContact bullet{1,0,10,0,1,0,true,true};
    const auto launched=attribution.observe(cast,cast,{}, {bullet});
    assert(launched.valid()&&launched.action==561);
    attribution.observe(cast,idle,{bullet},{bullet});
    auto hitBullet=bullet;hitBullet.collision=1;hitBullet.limit=0;
    const auto landed=attribution.observe(idle,idle,{bullet},{hitBullet});
    assert(landed==launched); // delayed 236C hit retains the casting action

    RecordTracker records;
    records.observeDamage(500,landed.action,10,landed.instance);
    // An idle body and long spacing must not split one cast's full damage.
    records.observeDamage(366,landed.action,40,landed.instance);
    records.observeDamage(336,landed.action,70,landed.instance);
    records.observeDamage(310,landed.action,100,landed.instance);
    records.observeDamage(285,landed.action,130,landed.instance);
    assert(records.best().damage==1797&&records.best().actionId==561);
    attribution.observe(idle,idle,{hitBullet},{});
    bullet.generation=11;
    const auto second=attribution.observe(cast,cast,{}, {bullet});
    assert(second.instance!=launched.instance);
    records.observeDamage(500,second.action,160,second.instance);
    assert(records.best().damage==1797); // same command twice is not one move

    ActorContact held=cast;held.sequence=1;held.animationFrame=40;
    attribution.observe(cast,held,{bullet},{bullet});
    ActorContact recast=cast;recast.sequence=0;recast.animationFrame=0;
    ProjectileContact recastBullet{3,0,15,0,1,0,true,true};
    attribution.observe(held,recast,{bullet},{bullet});
    attribution.observe(recast,recast,{bullet},{bullet,recastBullet});
    ActorContact rain;rain.action=571;
    ProjectileContact rainBullet{2,0,12,0,1,0,true,true};
    const auto rainCast=attribution.observe(rain,rain,{bullet},{bullet,rainBullet});
    auto rainHit=rainBullet;rainHit.collision=1;rainHit.limit=0;
    auto bothHit=bullet;bothHit.collision=1;bothHit.limit=0;
    const auto ambiguous=attribution.observe(rain,rain,{bullet,rainBullet},{bothHit,rainHit});
    assert(!ambiguous.valid()); // cannot steal an older bullet for the new cast
    records.observeDamage(242,571,180,100);
    records.observeDamage(233,571,181,100);
    records.observeDamage(2300,571,220,100);
    assert(records.best().damage==2775&&records.best().actionId==571);

    HitAttribution foreign;
    bullet.owned=false;bothHit.owned=false;
    assert(!foreign.observe(cast,cast,{bullet},{bothHit}).valid());
    assert(observedDamage(10000,10000,{18,4416,true},{18,4416,true})==0);
    assert(observedDamage(10000,10000,{18,4416,true},{1,242,false})==242);
    assert(observedDamage(9758,9525,{1,242,false},{2,475,false})==233);
    assert(observedDamage(10000,9900,{0,0,true},{0,0,true})==100); // chip
    assert(observedDamage(20,0,{0,0,true},{1,500,false})==20); // HP clamp
    // An unresolvable frame is observed but is NOT a move: it must never
    // become the BEST HIT (that path rendered "UNKNOWN MOVE" for Suika).
    RecordTracker unknown;
    unknown.observeDamage(500,-1,1,0);
    assert(unknown.best().damage==0&&!unknown.best().attributed);
    assert(unknown.unattributedDamage()==500);
    // A 4-cost Utsuho sun keeps generating child bullets after the fighter
    // becomes idle and even just after an emitter despawns. A child with an
    // explicit parent must inherit the ORIGINAL SC606 cast, not a new action.
    HitAttribution sun;
    ActorContact sunCast;sunCast.action=606;sunCast.animationFrame=10;
    ProjectileContact sunRoot{101,0,1,0,1,0,true,true};
    const auto castSun=sun.observe(sunCast,sunCast,{}, {sunRoot});
    assert(castSun.valid()&&castSun.action==606);
    sun.observe(sunCast,idle,{sunRoot},{sunRoot});
    sun.observe(idle,idle,{sunRoot},{}); // root despawns
    ProjectileContact sunChild{102,101,1,0,1,0,true,true};
    const auto childSpawn=sun.observe(idle,idle,{}, {sunChild});
    assert(childSpawn==castSun); // parent tombstone, no actor action
    auto hitSunChild=sunChild;hitSunChild.collision=1;hitSunChild.limit=0;
    const auto childContact=sun.observe(idle,idle,{sunChild},{hitSunChild});
    assert(childContact==castSun);
    RecordTracker sustained;
    sustained.observeDamage(600,castSun.action,10,castSun.instance);
    sustained.observeDamage(500,childContact.action,90,childContact.instance);
    sustained.observeDamage(700,childContact.action,160,childContact.instance);
    sustained.observeDamage(900,childContact.action,230,childContact.instance);
    assert(sustained.best().damage==2700); // post-untech, same cast
    // Meiling Rainbow Taichi has the same cast-lifetime principle.
    RecordTracker rainbow;
    rainbow.observeDamage(400,608,10,5);
    rainbow.observeDamage(600,608,90,5);
    rainbow.observeDamage(700,608,170,5);
    assert(rainbow.best().damage==1700&&rainbow.best().actionId==608);
    rainbow.observeDamage(900,608,190,6); // second cast: not merged
    assert(rainbow.best().damage==1700);
    // Tombstones are deliberately short-lived: an unrelated projectile
    // appearing much later must NOT steal a previous 4-card spell identity.
    HitAttribution expired;
    auto priorCast=expired.observe(sunCast,sunCast,{}, {sunRoot});
    assert(priorCast.valid());
    expired.observe(sunCast,idle,{sunRoot},{});
    for(int i=0;i<95;++i)expired.observe(idle,idle,{},{});
    assert(!expired.observe(idle,idle,{}, {sunChild}).valid());
}

static void testSuikaDelayed5CWithJ6A() {
    using namespace whm;
    // SokuLib Action.hpp: ACTION_5C=410, ACTION_j6A=307.
    HitAttribution source;
    ActorContact shoot; shoot.action=410;shoot.animationFrame=12;
    ActorContact idle;
    ProjectileContact fiveC{901,0,12,0,3,0,true,true};
    const auto spawn=source.observeDetailed(shoot,shoot,{}, {fiveC});
    assert(spawn.origin.valid()&&spawn.origin.action==410&&!spawn.mixed);
    source.observe(shoot,idle,{fiveC},{fiveC});
    auto hit5C=fiveC;hit5C.collision=1;hit5C.limit=2;
    const auto landed=source.observeDetailed(idle,idle,{fiveC},{hit5C});
    assert(landed.origin==spawn.origin&&!landed.mixed);
    RecordTracker record;
    record.observeDamage(2280,landed.origin.action,100,landed.origin.instance);
    assert(record.best().damage==2280&&record.best().actionId==410);

    // A hit from j6A is explicitly detected, but the *old* 5C projectile
    // still owns an active collision box. Cannot split the opponent's total
    // damage into the two attacks using HP/combo counters alone.
    ActorContact jump; jump.action=307;jump.collision=1;
    jump.limit=2;jump.hitBoxes=true;
    const auto overlap=source.observeDetailed(idle,jump,{hit5C},{hit5C});
    assert(overlap.mixed&&!overlap.origin.valid());
    // Before the fix the combined 2280+777 was wrongly ranked as a 3057 j6A.
    record.observeDamage(3057,overlap.origin.action,101,overlap.origin.instance,
                         overlap.mixed);
    assert(record.best().damage==2280&&record.best().actionId==410);
    assert(record.unattributedDamage()==3057);

    // With reliable frame-local HP delta, j6A adds only 777 even if the
    // combo counter changes its hit count/reset behavior.
    assert(observedDamage(7720,6943,{3,2280,false},{4,3057,false})==777);
    assert(observedDamage(7720,6943,{3,2280,false},{1,3057,false})==777);
    assert(observedDamage(10000,10000,{3,2280,false},{1,3057,false})==0);

    // Without old projectiles a genuine j6A-only hit still ranks normally.
    HitAttribution bodyOnly;
    auto j=bodyOnly.observeDetailed(idle,jump,{},{});
    assert(j.origin.valid()&&j.origin.action==307&&!j.mixed);
    RecordTracker clean;
    clean.observeDamage(777,j.origin.action,1,j.origin.instance,j.mixed);
    assert(clean.best().damage==777&&clean.best().actionId==307);

    // Two independent projectiles hitting together have no single owner.
    HitAttribution bullets;
    auto shotA=bullets.observe(shoot,shoot,{}, {fiveC});
    assert(shotA.valid());
    ProjectileContact second{902,0,13,0,3,0,true,true};
    ActorContact other;other.action=411;other.animationFrame=4;
    bullets.observe(other,other,{fiveC},{fiveC,second});
    auto aHit=fiveC,bHit=second;
    aHit.collision=1;aHit.limit=2;bHit.collision=1;bHit.limit=2;
    const auto two=bullets.observeDetailed(idle,idle,{fiveC,second},{aHit,bHit});
    assert(two.mixed&&!two.origin.valid());
}

static void testDelayedBirthAndDormantProjectileSafety() {
    using namespace whm;
    ActorContact idle,shoot,jump;
    shoot.action=410;shoot.sequence=1;shoot.animationFrame=10;
    jump.action=307;jump.sequence=1;jump.animationFrame=5;
    jump.hitBoxes=true;jump.collision=1;jump.limit=2;
    ProjectileContact dormant{4001,0,10,0,0,0,false,true};

    // 5C's projectile remains present in the game but its damage box is
    // dormant until explosion. No contact flag changes on the projectile in
    // the exact frame j6A hits. The previous code silently ranked all
    // observed damage as the j6A alone; a *latent* 5C source must veto that.
    HitAttribution persistent;
    // Birth without contact is not a damage event; the object retains
    // its 5C creation identity internally until an actual hit occurs.
    assert(!persistent.observe(shoot,shoot,{}, {dormant}).valid());
    persistent.observe(shoot,idle,{dormant},{dormant});
    auto active5C=dormant;
    active5C.hitBoxes=true;active5C.limit=2;active5C.collision=1;
    const auto cast=persistent.observe(idle,idle,{dormant},{active5C});
    assert(cast.valid()&&cast.action==410);
    persistent.observe(idle,idle,{active5C},{dormant});
    const auto mixed=persistent.observeDetailed(idle,jump,{dormant},{dormant});
    assert(mixed.mixed&&!mixed.origin.valid());
    assert(mixed.liveProjectiles==1&&mixed.unownedOrigins==0);
    RecordTracker record;
    record.observeDamage(2280,cast.action,10,cast.instance);
    record.observeDamage(3057,mixed.origin.action,20,mixed.origin.instance,mixed.mixed);
    assert(record.best().actionId==410&&record.best().damage==2280);

    // An unparented object that APPEARS after the owner is already in j6A
    // must not inherit j6A as the object's casting action. This also covers
    // delayed 5C's effect spawn after its player has changed animation.
    HitAttribution late;
    ProjectileContact firstLate{4002,0,11,0,0,0,false,true};
    auto uncertain=late.observeDetailed(jump,jump,{}, {firstLate});
    assert(uncertain.mixed&&!uncertain.origin.valid());
    assert(uncertain.unownedOrigins==1);
    auto lateHit=firstLate;
    lateHit.collision=1;lateHit.limit=1;lateHit.hitBoxes=true;
    uncertain=late.observeDetailed(jump,jump,{firstLate},{lateHit});
    assert(uncertain.mixed&&!uncertain.origin.valid());

    // Unknown parent pointer also must not be replaced with the currently
    // executing normal (before this patch it was assigned to j6A).
    HitAttribution unresolvedParent;
    ProjectileContact child{4003,0xdeadbeef,12,0,0,0,false,true};
    const auto ambiguous=unresolvedParent.observeDetailed(jump,jump,{}, {child});
    assert(ambiguous.mixed&&!ambiguous.origin.valid());
    assert(ambiguous.unownedOrigins==1);

    // With no competing delayed projectile, standalone j6A is still tracked.
    HitAttribution normal;
    const auto justNormal=normal.observeDetailed(idle,jump,{},{});
    assert(justNormal.origin.valid()&&justNormal.origin.action==307&&!justNormal.mixed);
}

static void testMixedDamageNeverBecomesBestRecord() {
    using namespace whm;
    // Regression from DamageTrace.log: an unresolvable 5C+j6A frame is a
    // combo total, not a move. It must be kept as observed damage but NEVER
    // ranked, and NEVER rendered as "UNKNOWN MOVE" in the BEST HIT card.
    RecordTracker fallback;
    fallback.observeDamage(777,-1,1,0,true);
    assert(fallback.best().damage==0&&!fallback.best().attributed);
    assert(fallback.best().actionId==-1);
    assert(fallback.unattributedDamage()==777);
    fallback.observeDamage(3057,-1,2,0,true);
    assert(fallback.best().damage==0);
    assert(fallback.unattributedDamage()==3834);
    // Once a genuinely identified single cast exists, mixed damage cannot
    // falsely replace it even if the mixture is numerically larger.
    fallback.observeDamage(800,307,3,1);
    assert(fallback.best().damage==800&&fallback.best().actionId==307);
    fallback.observeDamage(3057,-1,4,0,true);
    assert(fallback.best().damage==800&&fallback.best().actionId==307);
    fallback.reset();
    assert(fallback.best().damage==0);
    // The invariant holds for arbitrary interleavings of mixed and clean hits.
    RecordTracker fuzz;
    for(int i=1;i<=200;++i) {
        const bool mixed=(i%3==0);
        fuzz.observeDamage(100+i,mixed?-1:(i%2?410:307),static_cast<unsigned>(i),
                           mixed?0:(i%2?1:2),mixed);
        assert(fuzz.best().attributed||fuzz.best().damage==0);
    }

    ActorContact shot, idle, body;
    shot.action=410;shot.animationFrame=12;
    body.action=307;body.hitBoxes=true;body.limit=2;body.collision=1;
    ProjectileContact p{1901,0,1,0,0,0,false,true};
    HitAttribution pending;
    // Merely creating a dormant bullet is NOT a damage event.
    const auto spawn=pending.observeDetailed(shot,shot,{}, {p});
    assert(!spawn.origin.valid());
    pending.observe(shot,idle,{p},{p});
    // Some explosions do not expose a changed collision flag when sampled
    // around a whole engine update. An old, unique 5C is still attributable
    // if it is the only remaining attack and real HP decreased.
    RecordTracker delayed;
    for(int i=0;i<3;++i) {
        const auto unique=pending.observeDetailed(idle,idle,{p},{p});
        assert(unique.origin.valid()&&unique.origin.action==410&&!unique.mixed);
        delayed.observeDamage(i==0?600:i==1?680:1000,
            unique.origin.action,10+i,unique.origin.instance);
    }
    assert(delayed.best().damage==2280&&delayed.best().actionId==410);
    // 5C is still present when the body attack connects. The combined HP
    // decrease is not a trustworthy j6A-only number, but it MUST be retained
    // as observed (unassigned) damage rather than discarded.
    const auto ambiguous=pending.observeDetailed(idle,body,{p},{p});
    assert(ambiguous.mixed);
    delayed.observeDamage(777,ambiguous.origin.action,15,ambiguous.origin.instance,
                          ambiguous.mixed);
    assert(delayed.best().actionId==410&&delayed.best().damage==2280);
    assert(delayed.unattributedDamage()==777);
}

static void testSpellFollowupAttribution() {
    using namespace whm;
    // SokuLib Action.hpp: every SC200..219 has an ALT_EFFECT in 650..669.
    // Meiling SC209: 609 (activation), 659 (follow-up) are the SAME card.
    static_assert(isSpellActivationAction(609));
    static_assert(isSpellFollowupAction(659));
    static_assert(spellCardForAction(609)==209);
    static_assert(spellCardForAction(659)==209);
    static_assert(spellCardForAction(658)==208);
    static_assert(cardForAction(17,659)==209);
    static_assert(cardForAction(17,609)==209);
    static_assert(cardForAction(17,669)==219);
    static_assert(cardForAction(17,670)==-1);
    static_assert(canonicalAttackAction(659)==609);
    static_assert(canonicalAttackAction(608)==608);
    HitAttribution source;
    ActorContact idle;
    ActorContact activation;
    activation.action=609;activation.sequence=1;activation.animationFrame=12;
    activation.collision=1;activation.limit=2;activation.hitBoxes=true;
    const auto first=source.observe(idle,activation,{},{});
    assert(first.valid()&&first.action==609);
    RecordTracker record;
    record.observeDamage(900,first.action,20,first.instance);

    ActorContact cinematic;
    cinematic.action=659;cinematic.sequence=0;cinematic.animationFrame=0;
    // Direct scripted strike may not expose an ordinary box transition.
    auto follow=source.observe(activation,cinematic,{},{});
    assert(follow==first);
    record.observeDamage(1300,follow.action,21,follow.instance);
    cinematic.sequence=4;cinematic.animationFrame=25;
    follow=source.observe(cinematic,cinematic,{},{});
    assert(follow==first);
    record.observeDamage(2000,follow.action,27,follow.instance);
    // ALT_EFFECT sequence reset is not a second card use.
    ActorContact nextSequence=cinematic;
    nextSequence.sequence=0;nextSequence.animationFrame=0;
    follow=source.observe(cinematic,nextSequence,{},{});
    assert(follow==first);
    record.observeDamage(500,follow.action,32,follow.instance);
    assert(record.best().damage==4700);
    assert(record.best().actionId==609&&record.best().isSpell);

    // A fresh SC209 activation after ALT_EFFECT is a distinct spell cast.
    ActorContact recast=activation;
    recast.sequence=0;recast.animationFrame=0;
    const auto second=source.observe(nextSequence,recast,{},{});
    assert(second.valid()&&second.action==609&&second.instance!=first.instance);
    record.observeDamage(700,second.action,70,second.instance);
    assert(record.best().damage==4700);
    source.observe(recast,idle,{},{});
    assert(!source.observe(idle,idle,{},{}).valid());

    // One final cinematic hit may occur during 659 -> idle.
    HitAttribution exitStage;
    const auto exitCast=exitStage.observe(idle,activation,{},{});
    exitStage.observe(activation,cinematic,{},{});
    assert(exitStage.observe(cinematic,idle,{},{})==exitCast);
    assert(!exitStage.observe(idle,idle,{},{}).valid());

    // Do not invent attribution from an idle fighter or ambiguous hits.
    HitAttribution ambiguous;
    ActorContact otherCast=activation;otherCast.action=608;
    ProjectileContact firstBullet{1,0,1,0,1,0,true,true};
    const auto oldCast=ambiguous.observe(otherCast,otherCast,{}, {firstBullet});
    assert(oldCast.valid()&&oldCast.action==608);
    ProjectileContact secondBullet{2,0,1,0,1,0,true,true};
    const auto newer=ambiguous.observe(activation,cinematic,{firstBullet},{firstBullet,secondBullet});
    // Two competing active attack sources: no attribution yet.
    assert(!newer.valid());
    auto hitOld=firstBullet;hitOld.collision=1;hitOld.limit=0;
    auto hitNew=secondBullet;hitNew.collision=1;hitNew.limit=0;
    assert(!ambiguous.observe(cinematic,cinematic,
        {firstBullet,secondBullet},{hitOld,hitNew}).valid());

    assert(std::string(hudText(HudTextKey::SpecialType,HudLanguage::English))=="SKILL");
    assert(std::string(hudText(HudTextKey::SpellType,HudLanguage::English))=="SC");
    assert(std::string(hudText(HudTextKey::SpecialType,HudLanguage::Chinese))=="必杀技");
    assert(std::string(hudText(HudTextKey::SpellType,HudLanguage::Japanese))=="スペカ");
}

static void testHudRoundingModes() {
    const int original=std::fegetround();
    for(int mode:{FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO}) {
        assert(std::fesetround(mode)==0);
        assert(whm::hudFactor(1.1,2)=="1.10");
        assert(whm::hudFactor(1.2,2)=="1.20");
        assert(whm::hudFactor(1.3,2)=="1.30");
        assert(whm::hudFactor(1.4,2)=="1.40");
        assert(whm::hudFactor(1.25,2)=="1.25");
        assert(whm::gutsLabel(.7)=="×0.70");
        assert(std::fegetround()==mode); // HUD never changes game FP control
    }
    assert(std::fesetround(original)==0);
}

static void testPanelLayouts() {
    using namespace whm::layout;
    assert((physicalWorkArea({0,0,960,515},{0,0,1920,1080},true)==Rect{0,0,1920,1030}));
    assert((physicalWorkArea({0,0,960,515},{0,0,1920,1080},false)==Rect{0,0,960,515}));
    assert((physicalWorkArea({0,0,960,1080},{0,0,1920,1080},true)==Rect{0,0,960,1080}));
    assert((physicalWorkArea({0,0,1920,1030},{0,0,1920,1080},true)==Rect{0,0,1920,1030}));
    assert((physicalWorkArea({-960,0,960,515},{-1920,0,1920,1080},true)==Rect{-1920,0,1920,1030}));

    for(auto area:std::vector<Rect>{{0,0,1920,1040},{0,0,2560,1400},{0,0,1280,800},{0,0,1366,728},{0,0,1024,728},{1920,-80,1920,1080}}) {
        for(auto border:std::vector<Insets>{{8,31,8,8},{0,0,0,0}}) {
            Input input;input.area=area;input.borders=border;
            input.original={area.x+40,area.y+20,1280+border.left+border.right,960+border.top+border.bottom};
            auto result=plan(input);
            assert(result.game.x==input.original.x&&result.game.y==input.original.y);
            assert((result.game.w-border.left-border.right)*3==(result.game.h-border.top-border.bottom)*4);
            assert(result.panel.y==result.game.y+result.game.h);
            if(result.game.w!=input.original.w)assert(result.panel.y+result.panel.h<=area.y+area.h-8);
            // Largest fitting integral 4:3 scale: no huge unused bottom space.
            if(result.game.w!=input.original.w)assert(area.y+area.h-result.panel.y-result.panel.h<16);
            for(int n=0;n<100;++n)assert(plan(input).game==result.game);
            input.autoFit=false;
            for(auto dock:{Dock::Bottom,Dock::Left,Dock::Right}) {
                input.dock=dock;input.vertical=dock!=Dock::Bottom;
                auto following=plan(input);assert(following.game==input.original);
                if(dock!=Dock::Bottom) {
                    assert(following.panel.h==following.game.h&&following.panel.y==following.game.y);
                    if(dock==Dock::Right)assert(following.panel.x==following.game.x+following.game.w);
                    else assert(following.panel.x+following.panel.w==following.game.x);
                }
            }
        }
    }
    Input fits;fits.area={0,0,1920,1040};fits.original={351,125,640,480};
    assert(plan(fits).game==fits.original);
    auto slight=fits;slight.original.y=1040-480-186+24;
    assert(plan(slight).game==slight.original); // small overflow is tolerated
    auto tight=fits;tight.original={351,125,1280,960};
    auto reduced=plan(tight);assert(reduced.game.x==351&&reduced.game.y==125);
    assert(reduced.panel.y+reduced.panel.h<=1032&&reduced.panel.y+reduced.panel.h>=1024);
    tight.unitStep=2;auto dpiFit=plan(tight);
    assert((dpiFit.game.w/4)%2==0&&(dpiFit.game.h/3)%2==0);
    assert(dpiFit.panel.y+dpiFit.panel.h<=1032);
    assert(portraitHeight(240,500)==833); // no minimum that squashes text/icons
    for(Rect display:std::vector<Rect>{{0,0,1920,1080},{-1920,80,1920,1080},{1920,0,2560,1440}}) {
        Input full;full.area=display;full.original={display.x+240,display.y,1440,1080};full.fullscreen=true;
        auto centered=plan(full);
        assert(std::abs((centered.game.x-display.x)*2+centered.game.w-display.w)<=1);
        assert(std::abs((centered.game.y-display.y)*2+centered.game.h+centered.panel.h-display.h)<=1);
        assert(centered.game.y>=display.y+8&&centered.panel.y+centered.panel.h<=display.y+display.h-8);
        full.original=centered.game;assert(plan(full).game==centered.game); // no repeated shrinking
    }
    FullscreenEntry entry;Rect display{0,0,1920,1080},native{240,0,1440,1080};
    assert(!entry.observe(true,{100,100,800,600},display,100));
    assert(!entry.observe(true,native,display,150));
    assert(!entry.observe(true,native,display,320));
    assert(entry.observe(true,native,display,340));
    assert(!entry.observe(true,{450,9,1020,765},display,600)); // own fit, no entry loop
    assert(!entry.observe(false,{100,100,800,600},display,700));
    assert(!entry.observe(true,native,display,800));assert(entry.observe(true,native,display,1000));
    Input wide;wide.original={240,0,1440,1080};wide.area={0,0,1920,1080};
    wide.fullscreen=true;wide.vertical=true;wide.dock=Dock::Right;
    assert(plan(wide).game.w==1440&&plan(wide).game.h==1080);
    auto point=canonicalPoint(true,320,20);assert(point.first==916&&point.second==214);
    Input flush;flush.area={0,0,1920,1080};flush.original={300,60,960,720};flush.autoFit=false;
    assert(plan(flush).panel.y==780); // no artificial gap at bottom
    flush.dock=Dock::Right;flush.vertical=true;
    assert(plan(flush).panel.x==1260&&plan(flush).panel.y==60&&plan(flush).panel.h==720);
    flush.dock=Dock::Left;assert(plan(flush).panel.x+plan(flush).panel.w==300);
    flush.original={0,0,1920,1080};assert(plan(flush).game==flush.original&&plan(flush).panel.x==-plan(flush).panel.w); // never overlay the game
    const Rect game{300,100,960,720};
    assert(nearestDock(game,200,300)==Dock::Left);
    assert(nearestDock(game,1400,300)==Dock::Right);
    assert(nearestDock(game,700,950)==Dock::Bottom);
    assert(nearestDock(game,700,700)==Dock::Bottom);
    for(auto d:{Dock::Bottom,Dock::Left,Dock::Right}) {
        auto alternatives=alternateDocks(d);assert(alternatives[0]!=d&&alternatives[1]!=d&&alternatives[0]!=alternatives[1]);
    }
    auto plus=foldedButton(game,{8,31,8,8});assert(plus.x+plus.w==game.x+game.w-16&&plus.y+plus.h==game.y+game.h-16);
    for(int n=0;n<4;++n){auto b=portraitButtons[n];auto mapped=canonicalPoint(true,b.x+14,b.y+14);
        assert(mapped.first==924+(n%2)*34&&mapped.second==218+(n/2)*34);}
    assert(canonicalPoint(true,10,5).first==-1); // draggable header, not a button
    const Rect desktop{0,0,1920,1080};
    assert(automaticDock({200,342,960,480},desktop,Dock::Bottom,240)==Dock::Bottom); // ~20px overflow

    assert(automaticDock({200,450,960,600},desktop,Dock::Bottom,240)==Dock::Right);
    assert(automaticDock({700,450,960,600},desktop,Dock::Bottom,240)==Dock::Left);
    assert(automaticDock({10,20,960,720},desktop,Dock::Left,240)==Dock::Right);
    assert(automaticDock({940,20,960,720},desktop,Dock::Right,240)==Dock::Left);
    for(Rect r:std::vector<Rect>{{0,0,1920,1080},{10,0,1900,1080},{120,0,1680,1080}}) {
        auto d=automaticDock(r,desktop,Dock::Bottom,240);
        for(int n=0;n<100;++n)assert(automaticDock(r,desktop,d,240)==d);
    }
    assert(nearDock(game,game.x-12,300,Dock::Left));
    assert(!nearDock(game,game.x-100,300,Dock::Left));
    PendingResize pending;Rect original{0,0,1280,960},target{0,0,960,720};
    assert(pending.request(original,target,100));
    assert(!pending.request(original,{0,0,800,600},200)); // no queued resize race
    assert(pending.ready(target,300)&&!pending.pending);
    assert(!pending.request(target,target,400));
    assert(pending.request(target,original,500));
    assert(pending.ready(target,2100)&&pending.blocked);
    assert(!pending.request(target,original,2200)); // bounded, no resize loop
}

// The 2026-11-15 run recorded AFTER the BEST HIT invariant fix reproduced
// the user's complaint exactly: frames 101/102/103/119 were the FIRST hits
// of a fresh session, all source=-1:0 mixed=1, and best stayed -1:0 (the
// panel showed NO HIT RECORDED). Root cause was HitAttribution::born()
// treating the tail of the SAME 5C bullet stream as orphan projectiles as
// soon as the actor cancelled into j6A (307). Those bullets are one cast,
// not a second independent damage source.
static void testSuika5CStreamAfterJ6AKeepsItsCast() {
    using namespace whm;
    ActorContact shoot;shoot.action=410;shoot.animationFrame=12;
    ActorContact jump;jump.action=307;jump.animationFrame=5;
    ActorContact idle;
    HitAttribution source;
    ProjectileContact head{9001,0,101,0,1,0,false,true};
    const auto cast=source.observeDetailed(shoot,shoot,{},{head});
    // A dormant spawn is not yet a damage event, but it MUST be owned.
    assert(cast.unownedOrigins==0&&cast.liveProjectiles==1);

    // 5C keeps emitting unparented bullets for ~25 frames while the owner is
    // already in j6A. They must inherit the SAME 5C cast, never become
    // orphans (which used to force mixed=1 and blank the BEST HIT).
    std::vector<ProjectileContact> live{head};
    for(int i=0;i<6;++i) {
        ProjectileContact tail{static_cast<std::uintptr_t>(9100+i),0,
                               static_cast<unsigned>(200+i),0,1,0,false,true};
        std::vector<ProjectileContact> post=live;
        post.push_back(tail);
        const auto seen=source.observeDetailed(jump,jump,live,post);
        assert(seen.unownedOrigins==0);
        live.push_back(tail);
    }
    // Delayed 5C explosion: no actor box, no collision flag transition on
    // the sampled frame, but the stream is the only credible source.
    std::vector<ProjectileContact> exploding;
    for(const auto &p:live){auto q=p;q.hitBoxes=true;q.limit=1;exploding.push_back(q);}
    const auto landed=source.observeDetailed(idle,idle,live,exploding);
    assert(landed.origin.valid()&&landed.origin.action==410&&!landed.mixed);

    RecordTracker record;
    record.observeDamage(2280,landed.origin.action,655,landed.origin.instance,
                         landed.mixed);
    assert(record.best().attributed&&record.best().actionId==410);
    assert(record.best().damage==2280);

    // j6A adds 777 on top (combo 18:2280>19:3057). A DIFFERENT origin has
    // an active attack box this frame, so the frame stays mixed and 777 must
    // NOT inflate 5C back to the bogus 3057 single-move record.
    ActorContact body=jump;body.hitBoxes=true;body.collision=1;body.limit=2;
    const auto overlap=source.observeDetailed(idle,body,exploding,exploding);
    assert(overlap.mixed&&!overlap.origin.valid());
    record.observeDamage(777,overlap.origin.action,662,
                         overlap.origin.instance,overlap.mixed);
    assert(record.best().damage==2280&&record.best().actionId==410);
    assert(record.unattributedDamage()==777);
}

// Replay of the real overlap-first session (DamageTrace.log lines 982..1253)
// with the FIXED attribution. The user's exact repro must never leave the
// BEST HIT card blank, and 3057 must never be ranked as one 5C.
static void testOverlapFirstSessionNeverBlank() {
    using namespace whm;
    struct Event { unsigned frame; int damage; int action; std::uint64_t instance; bool mixed; };
    const Event events[] = {
        {101,1244,410,1,false},  // combo 0:0>9:1244, now credited to 5C
        {102,684,-1,0,true},
        {103,835,-1,0,true},
        {119,69,-1,0,true},
        {424,2280,410,3,false}, // clean 5C 0:0>18:2280
        {525,1200,307,4,false}, // clean j6A
        {655,2280,410,5,false}, // overlap 5C head, now credited to 5C
        {662,777,-1,0,true},    // j6A add-on, must stay mixed
    };
    RecordTracker record;
    int unattributed=0;
    for(const auto &e:events) {
        record.observeDamage(e.damage,e.action,e.frame,e.instance,e.mixed);
        assert(record.best().attributed&&record.best().actionId==410);
        assert(record.best().damage>=1244); // never NO HIT RECORDED
        if(e.mixed)unattributed+=e.damage;
        assert(record.unattributedDamage()==unattributed);
    }
    assert(record.best().damage==2280);
}

// Replay of the two real Suika practice rounds captured in DamageTrace.log.
// Every frame that actually dealt damage is here, in order, with the
// attribution result produced by the diagnostics build. It guards the
// RecordTracker invariant that unresolvable frames never become a record.
static void testSuikaDamageTraceReplay() {
    using namespace whm;
    struct Event { unsigned frame; int damage; int action; std::uint64_t instance; bool mixed; };
    // 410 = 5C, 307 = j6A. Overlap frames had source=-1:0 mixed=1 with
    // unresolved=20..32, i.e. deliberately unresolvable 5C+j6A combos.
    const Event round1[] = {
        {282,2280,410,1,false},
        {420,1200,307,2,false},
        {732,2280,-1,0,true},
        {735,777,-1,0,true},
    };
    const Event round2[] = {
        {125,2280,410,1,false},
        {271,1200,307,2,false},
        {517,1956,-1,0,true},
        {518,206,-1,0,true},
        {520,795,-1,0,true},
        {543,65,-1,0,true},
    };
    RecordTracker record;
    auto replay=[&](const Event *events,int count) {
        int unattributed=0;
        for(int i=0;i<count;++i) {
            const auto &e=events[i];
            record.observeDamage(e.damage,e.action,e.frame,e.instance,e.mixed);
            const auto &best=record.best();
            // Never an unattributed "UNKNOWN MOVE", not even transiently.
            assert(best.attributed&&best.damage==2280&&best.actionId==410);
            assert(!best.isSpell);
            if(e.mixed)unattributed+=e.damage;
            assert(record.unattributedDamage()==unattributed);
        }
    };
    replay(round1,4);
    // DamageTrace.log shows best=-1:0 at frames 74..124: entering the next
    // round re-creates the BattleManager and resets the tracker.
    record.reset();
    replay(round2,6);

    // The exact user repro: a fresh practice session whose FIRST hits are
    // the 5C+j6A overlap. There is no clean single cast to fall back on,
    // so no record may be fabricated.
    RecordTracker overlapFirst;
    overlapFirst.observeDamage(1956,-1,517,0,true);
    overlapFirst.observeDamage(206,-1,518,0,true);
    overlapFirst.observeDamage(795,-1,520,0,true);
    overlapFirst.observeDamage(65,-1,543,0,true);
    assert(overlapFirst.best().damage==0&&!overlapFirst.best().attributed);
    assert(overlapFirst.unattributedDamage()==3022);
    // A clean 5C later in the same session restores a correct BEST HIT.
    overlapFirst.observeDamage(2280,410,600,1,false);
    assert(overlapFirst.best().damage==2280&&overlapFirst.best().actionId==410);
}

// Root fix for the WHOLE class of "a real hit leaves the panel blank":
// the game's own combo counter proves damage happened even when no cast can
// be identified. Its value is authoritative, so the panel can show a real
// number instead of NO HIT RECORDED, without inventing a move name.
static void testNoBlankBestHitAfterAnyObservedDamage() {
    using namespace whm;
    // Validation run 2026-11-15, frames 983..1023: six consecutive hits of
    // a 500-series skill, every one source=-1:0 mixed=1. A player landing
    // ONLY these used to see an empty BEST HIT card.
    RecordTracker allMixed;
    const int combo[]={400,626,1288,1391,1486,1573};
    const int delta[]={400,226,662,103,95,87};
    for(int i=0;i<6;++i) {
        allMixed.observeDamage(delta[i],-1,983+i*5,0,true);
        allMixed.observeCombo(combo[i]);
    }
    assert(allMixed.best().damage==0&&!allMixed.best().attributed);
    assert(allMixed.hasObservedDamage());
    assert(allMixed.comboPeak()==1573);
    assert(allMixed.unattributedDamage()==1573);
    // A later verified single cast takes over. The combo peak never becomes
    // a move record and never inflates one.
    allMixed.observeDamage(2280,410,1100,7,false);
    allMixed.observeCombo(2280);
    assert(allMixed.best().damage==2280&&allMixed.best().actionId==410);
    assert(allMixed.comboPeak()==2280);

    // Combo counter unavailable: frame-local HP deltas alone must still
    // prove that damage happened.
    RecordTracker hpOnly;
    hpOnly.observeDamage(1956,-1,517,0,true);
    hpOnly.observeDamage(206,-1,518,0,true);
    assert(hpOnly.comboPeak()==0&&hpOnly.hasObservedDamage());
    assert(hpOnly.unattributedDamage()==2162);

    // Only a genuinely untouched tracker may show NO HIT RECORDED.
    RecordTracker untouched;
    assert(!untouched.hasObservedDamage());
    untouched.observeCombo(0);
    untouched.observeDamage(0,-1,2,0,true);
    assert(!untouched.hasObservedDamage());
    allMixed.reset();
    assert(!allMixed.hasObservedDamage()&&allMixed.comboPeak()==0);
    assert(allMixed.best().damage==0);
}


// Reverse-solve real DamageTrace.log combo totals with Cleista per-hit data.
// The record number is the CUMULATIVE combo damage, not a frame delta.
static void testDamageInferenceSuikaComboReplay() {
    using namespace whm;
    std::istringstream src(R"HPROF(# character|action|card|kind|notation|neutralTotal|perHitBase|hitCount|prorationPermille
9|300|-1|normal|5A|500|500|1|980
9|301|-1|normal|f.5A|1000|1000|1|980
9|301|-1|normal|f.5[A]|1250|1250|1|980
9|302|-1|normal|6A|1000|1000|1|980
9|302|-1|normal|6[A]|1150|1150|1|980
9|303|-1|normal|2A|550|550|1|980
9|304|-1|normal|3A|900|900|1|980
9|304|-1|normal|3[A]|1100|1100|1|980
9|305|-1|normal|66A|1071|450|2|980
9|306|-1|normal|j.5A|600|600|1|980
9|307|-1|normal|j.6A|1200|1200|1|980
9|308|-1|normal|j.2A|400|400|1|980
9|309|-1|normal|j.8A|817|250|4|980
9|320|-1|normal|5AA|600|600|1|980
9|321|-1|normal|5AAA|850|850|1|980
9|322|-1|normal|5AAAA|1200|1200|1|980
9|330|-1|normal|4A|500|500|1|980
9|400|-1|normal|5B|400|400|1|980
9|400|-1|normal|5[B]|1071|400|3|980
9|401|-1|normal|6B|832|100|2|980
9|402|-1|normal|2B|1000|1000|1|980
9|402|-1|normal|2[B]|1100|1100|1|980
9|404|-1|normal|j.5B|400|400|1|980
9|404|-1|normal|j.5[B]|1071|400|3|980
9|405|-1|normal|j.6B|832|100|2|980
9|406|-1|normal|j.2B|832|100|2|980
9|408|-1|normal|66B|1191|500|3|980
9|410|-1|normal|5C|2280|150|18|980
9|411|-1|normal|6C|2202|150|18|980
9|412|-1|normal|2C|2202|150|18|980
9|414|-1|normal|j.5C|2217|150|18|980
9|415|-1|normal|j.6C|2201|150|18|980
9|418|-1|normal|66C|900|900|1|980
9|500|100|skill|236B|2058|1200|7|980
9|500|100|skill|236[B]|1311|1200|3|980
9|501|100|skill|236C|2459|1000|9|980
9|501|100|skill|236[C]|1029|1000|2|980
9|505|104|skill|236B|880|880|1|980
9|505|104|skill|236[B]|2200|2000|1|980
9|506|104|skill|236C|880|880|1|980
9|506|104|skill|236[C]|2200|2000|1|980
9|507|104|skill|j.236B|880|880|1|980
9|507|104|skill|j.236[B]|2200|2000|1|980
9|508|104|skill|j.236C|880|880|1|980
9|508|104|skill|j.236[C]|2200|2000|1|980
9|510|108|skill|236B|1485|1485|1|980
9|511|108|skill|236C|1485|1485|1|980
9|512|108|skill|j.236B|1485|1485|1|980
9|513|108|skill|j.236C|1485|1485|1|980
9|520|101|skill|623B|2187|400|3|980
9|521|101|skill|623C|2187|400|3|980
9|522|101|skill|j.623B|2179|600|3|980
9|523|101|skill|j.623C|2179|600|3|980
9|525|105|skill|623B|1719|550|7|980
9|526|105|skill|623C|2035|550|9|980
9|530|109|skill|623B|880|880|1|980
9|531|109|skill|623C|880|880|1|980
9|540|102|skill|214B|1024|200|6|980
9|541|102|skill|214C|1032|200|6|980
9|542|102|skill|j.214B|220|220|1|980
9|543|102|skill|j.214C|616|220|3|980
9|550|110|skill|214B|1119|440|3|980
9|551|110|skill|214C|1615|440|5|980
9|565|107|skill|22B|1625|132|15|980
9|565|107|skill|22[B]|732|120|6|980
9|566|107|skill|22C|1639|132|15|980
9|566|107|skill|22[C]|393|120|3|980
9|570|111|skill|22B|1633|816|2|980
9|571|111|skill|22C|1633|816|2|980
9|600|200|spell|sc200|2000|2000|1|980
9|602|202|spell|sc202|3000|3000|1|980
9|603|203|spell|sc203|5000|2500|2|980
9|605|205|spell|sc205|1500|1500|5|980
9|606|206|spell|sc206|1029|300|5|980
9|607|207|spell|sc207|4371|2000|5|980
9|608|208|spell|sc208|6515|1628|4|980
9|612|212|spell|sc212|3263|1200|3|980
9|650|200|spell|sc200|2000|2000|1|980
)HPROF");
    DamageInference catalog;
    std::vector<std::string> errors;
    assert(catalog.read(src,&errors));
    assert(errors.empty());
    assert(catalog.size()==78);
    auto state=[&](int hits,std::vector<int> evidence,int actor) {
        InferenceState s;
        s.character=9;
        s.defenderHP=10000;
        s.comboHits=hits;
        s.comboRate=1.0;
        s.attack=1.;s.defense=1.;
        s.skillMultiplier=1.;s.spellMultiplier=1.;
        s.weather=0;s.typeIncludesWeather=true;
        s.actorAction=actor;
        s.originActions=std::move(evidence);
        return s;
    };
    // Clean 5C: 18 bullets of 150, measured 2280 total.
    const auto clean=catalog.solve(2280,state(18,{410},-1));
    assert(clean.valid&&clean.primaryAction==410&&clean.primaryDamage==2280);
    assert(clean.confidence>0.9&&clean.parts.size()==1);
    // The exact regression: 5C 2280 + j6A 777 = 3057, 19 hits.
    // It must NEVER be ranked as one 3057 j6A, nor left blank.
    const auto overlap=catalog.solve(3057,state(19,{410,307},307));
    assert(overlap.valid);
    assert(overlap.primaryAction==410);
    assert(overlap.primaryDamage==2280);
    assert(overlap.primaryDamage<3057);
    assert(overlap.parts.size()==2);
    bool hasJ6A=false;
    for(const auto &p:overlap.parts)
        if(p.action==307&&p.damage>=740&&p.damage<=810)hasJ6A=true;
    assert(hasJ6A);
    // Plain body hit is the easiest explanation.
    const auto body=catalog.solve(1200,state(1,{307},307));
    assert(body.valid&&body.primaryAction==307&&body.primaryDamage==1200);
    // Another character move: 6C is 150 x4 -> 578.
    const auto sixC=catalog.solve(578,state(4,{411},-1));
    assert(sixC.valid&&sixC.primaryAction==411&&sixC.primaryDamage==578);
    // Cleista measured j.5C landing only 8 of its 18 bullets (1107), but
    // the Steam Deck log shows a real 18-bullet j.5C at 2280. BOTH must be
    // explainable: treating the measured sample as a move ceiling made the
    // 2280 record get rejected as "impossible j.5C".
    const auto partialJ5C=catalog.solve(1107,state(8,{414},-1));
    assert(partialJ5C.valid&&partialJ5C.primaryAction==414&&
           partialJ5C.primaryDamage==1107);
    const auto fullJ5C=catalog.solve(2280,state(18,{414},-1));
    assert(fullJ5C.valid&&fullJ5C.primaryAction==414);
    assert(fullJ5C.primaryDamage>=2150&&fullJ5C.primaryDamage<=2300);
    assert(fullJ5C.confidence>0.9);
    // Fourth DamageTrace run: FIRST hit of the match was the overlap.
    // 9 hits/1244, 15/1928, 16/2763, 17/2832 all resolve to the 5C stream.
    for(const auto &sample:std::vector<std::pair<int,int>>{
            {1244,9},{1928,15},{2763,16},{2832,17}}) {
        const auto r=catalog.solve(sample.first,
            state(sample.second,{410,307},307));
        assert(r.valid);
        assert(r.primaryAction==410);
        // A simulated single-cast share may be slightly above the measured
        // total; what matters is a small residual and a real 5C cast.
        assert(r.residual<=static_cast<int>(sample.first*0.06)+30);
        assert(r.confidence>0.6);
    }
    // No forced guesses. 9000 can be approximated by sc208 x4 + 5C x15
    // (8006), but sc208 never appeared in this combo: an unmatched pair with
    // a large residual must be rejected, not shown as "sc208 (EST.)".
    const auto impossible=catalog.solve(9000,state(18,{410},-1));
    assert(!impossible.valid);
    DamageInference empty;
    assert(!empty.solve(2280,state(18,{410},-1)).valid); // missing data table
    // Simulated explanations NEVER pollute the verified BEST HIT invariant.
    RecordTracker tracker;
    tracker.observeInferred(410,-1,2280,0.9);
    assert(tracker.best().damage==0&&!tracker.best().attributed);
    assert(tracker.inferred().valid&&tracker.inferred().actionId==410);
    assert(tracker.inferred().damage==2280);
    tracker.observeInferred(307,-1,1200,0.9); // weaker must not overwrite
    assert(tracker.inferred().damage==2280);
    tracker.observeDamage(500,400,1,1,false); // a real verified cast wins
    assert(tracker.best().attributed&&tracker.best().damage==500);
    tracker.reset();
    assert(!tracker.inferred().valid);
    // Call-site upper bound: a cast inside a combo can never exceed the
    // combo's own total, so a combo at/below the record cannot beat it.
    assert(whm::inferenceCanBeatRecord(1244,0));      // no verified record yet
    assert(whm::inferenceCanBeatRecord(3057,2280));
    assert(!whm::inferenceCanBeatRecord(1244,2280));
    assert(!whm::inferenceCanBeatRecord(2280,2280));  // equal cannot raise it
    // Sanity ceilings use the FULL projectile stream, not Cleista's single
    // measured run. j.5C spawns 18 bullets just like 5C (density "C1x18").
    assert(catalog.maxNeutralDamage(9,414)==2217);   // j.5C full stream
    assert(catalog.maxNeutralDamage(9,410)==2280);   // 5C = 150 x 18
    assert(catalog.maxNeutralDamage(9,607)==4371);   // spell sc207
    assert(catalog.maxNeutralDamage(9,9999)==0);
    assert(catalog.maxNeutralDamage(-1,410)==0);
}

// DamageTrace.log 2026-11-15 12:20, frames 19484/19639: a spell's 2000+1500
// was credited to j.5C (414) and became "j.5C 3500" because casts_[instance]
// had no upper bound at all. The ceiling must use the FULL 18-bullet stream
// (2217), NOT Cleista's one measured 8-bullet run (1107), or a legitimate
// 2280 j.5C would be wrongly retracted.
static void testRecordCeilingRejectsImpossibleCast() {
    using namespace whm;
    const int j5c=(2217*3)/2;   // 3325, same 1.5x rule as ModMain
    const int fiveC=(2280*3)/2; // 3420
    RecordTracker t;
    t.observeDamage(500,300,10,1,false,750);        // clean 5A
    assert(t.best().attributed&&t.best().actionId==300&&t.best().damage==500);
    t.observeDamage(2000,414,20,28,false,j5c);
    t.observeDamage(1500,414,30,28,false,j5c);
    assert(t.best().actionId==300&&t.best().damage==500); // no j.5C 3500
    assert(t.castDamage(28)==0);
    t.observeDamage(300,414,40,28,false,j5c);        // stays rejected
    assert(t.best().damage==500);
    assert(t.unattributedDamage()==3800);
    // If a bad cast had already written a record, rejecting it retracts it.
    RecordTracker retract;
    retract.observeDamage(2000,414,10,55,false,j5c);
    assert(retract.best().actionId==414&&retract.best().damage==2000);
    retract.observeDamage(1500,414,20,55,false,j5c); // 3500 > 3325
    assert(!retract.best().attributed);
    // The user's REAL 18-bullet j.5C (2280) must rank, not be rejected.
    RecordTracker realJ5C;
    realJ5C.observeDamage(1244,414,10,88,false,j5c);
    realJ5C.observeDamage(684,414,11,88,false,j5c);
    realJ5C.observeDamage(352,414,12,88,false,j5c);
    assert(realJ5C.best().attributed&&realJ5C.best().actionId==414);
    assert(realJ5C.best().damage==2280);
    // A legitimate 18-hit 5C stream must still rank normally.
    RecordTracker stream;
    stream.observeDamage(1244,410,10,7,false,fiveC);
    stream.observeDamage(684,410,11,7,false,fiveC);
    stream.observeDamage(352,410,12,7,false,fiveC);
    assert(stream.best().attributed&&stream.best().actionId==410);
    assert(stream.best().damage==2280);
    // Legacy callers without a ceiling keep the old behaviour.
    RecordTracker legacy;
    legacy.observeDamage(2300,571,220,100);
    assert(legacy.best().attributed&&legacy.best().damage==2300);
}

// Cleista lists charged variants as separate moves with a bracketed notation
// ("236[B]"). Steam Deck DamageTrace.log 13:18 has action=505 seq=2 dealing
// 880 (tap) at frame 648 and 2200 (charged) at frame 956: identical action
// and sequence, NO charge flag. Damage is the only way to tell them apart.
static void testChargedVariantLabels() {
    using namespace whm;
    assert(isChargedNotation("236[B]"));
    assert(isChargedNotation("j.236[C]"));
    assert(!isChargedNotation("236B"));
    assert(std::string(chargedSuffix(HudLanguage::Chinese))=="（蓄力）");
    assert(std::string(chargedSuffix(HudLanguage::Japanese))=="（溜め）");
    assert(std::string(chargedSuffix(HudLanguage::English))==" (CHARGE)");
    // Two equally strong charged buttons merge; the bracket must survive.
    assert(mergeSkillButtons("236[B]","236[C]")=="236[B/C]");
    assert(mergeSkillButtons("j.236[B]","j.236[C]")=="j.236[B/C]");
    assert(mergeSkillButtons("236[B]","236[B]")=="236[B]");
    assert(mergeSkillButtons("236B","236C")=="236B/C"); // tap unchanged
    assert(cardDisplayName("元鬼玉","236[B]")=="元鬼玉 · 236[B]");
    assert(cardDisplayName("元鬼玉","236B")=="元鬼玉 · 236B");
    // Tap 880 / charged 2200 share action 505. Select by measured damage.
    std::vector<NotationCandidate> oni={{"236B",880},{"236[B]",2200}};
    assert(pickNotationByDamage(oni,2200)=="236[B]");
    assert(pickNotationByDamage(oni,880)=="236B");
    assert(pickNotationByDamage(oni,0)=="236B"); // no measurement -> legacy tap
    assert(pickNotationByDamage({},2200).empty());
    // An unrelated measured value still picks the closest real variant.
    assert(pickNotationByDamage(oni,1900)=="236[B]");
}

// Marisa's delayed 4-card SC (DamageTrace.log 15:09, frame 13506):
//   dealt=4000 combo=1:4000 source=-1:0 mixed=1 hits=0 live=22 unresolved=22
//   infer=614:4000 conf=90 explain=[single sc214 x2] best=500:1350
// The caster is already idle (action=5) and every projectile is an orphan, so
// HitAttribution can never produce an owner; the verified record stayed 1350
// and the 4000 estimate was never allowed onto the BEST HIT card.
static void testEstimatesNeverBecomeActualRecord() {
    using namespace whm;
    const ActualRecord verified{1350,500,true,false,false};
    const InferredRecord delayed{true,614,214,4000,0.90};
    // The old failure: numerical similarity must NOT override a measured cast.
    assert(pickRecordSource(verified,delayed,4000,true)==RecordSource::Verified);
    // ...but best_ itself stays VERIFIED. Record pruning and future real hits
    // must keep using measured data, never a simulation.
    RecordTracker tracker;
    tracker.observeDamage(1350,500,1000,1);
    tracker.finish();
    tracker.observeCombo(4000);
    tracker.observeInferred(614,214,4000,0.90);
    assert(tracker.best().attributed&&tracker.best().damage==1350);
    assert(pickRecordSource(tracker.best(),tracker.inferred(),
                            tracker.comboPeak(),tracker.hasObservedDamage())
           ==RecordSource::Verified);
    // 5C/j.6A overlap must NOT regress: a tying or weaker estimate loses.
    const ActualRecord cast5C{2280,410,true,false,false};
    const InferredRecord tie{true,410,-1,2280,0.85};
    const InferredRecord weaker{true,307,-1,775,0.90};
    assert(pickRecordSource(cast5C,tie,3057,true)==RecordSource::Verified);
    assert(pickRecordSource(cast5C,weaker,3057,true)==RecordSource::Verified);
    // A weak guess must not replace a verified record.
    const InferredRecord weakGuess{true,614,214,4000,0.50};
    assert(pickRecordSource(verified,weakGuess,4000,true)==RecordSource::Verified);
    // A simulated number above ANY observed damage is a fitting artefact.
    const InferredRecord overshoot{true,614,214,5000,0.99};
    assert(pickRecordSource(verified,overshoot,4000,true)==RecordSource::Verified);
    // No verified cast: even a high-confidence estimate remains unassigned.
    assert(pickRecordSource(ActualRecord{},delayed,4000,true)==RecordSource::Unattributed);
    // Missing identity is explicitly unassigned, never a fabricated move name.
    assert(pickRecordSource(ActualRecord{},InferredRecord{},4000,true)==RecordSource::Unattributed);
    assert(pickRecordSource(verified,InferredRecord{},4000,true)==RecordSource::Verified);
    // Untouched tracker: NO HIT RECORDED remains correct.
    assert(pickRecordSource(ActualRecord{},InferredRecord{},0,false)==RecordSource::Empty);
    // A larger but implausible guess cannot displace the 4000@0.90 estimate.
    tracker.observeInferred(625,-1,5000,0.40);
    assert(tracker.inferred().damage==4000&&tracker.inferred().confidence==0.90);
    tracker.observeInferred(614,214,4500,0.88);
    assert(tracker.inferred().damage==4500);
    // Card captions: reserve " · COMMAND" first, ellipsize ONLY the name.
    auto measure=[](const std::wstring &s){return static_cast<int>(s.size()*10);};
    const auto fit=layoutCardTitle(L"Witch Ley Line · 214B",400,measure);
    assert(!fit.split); // fits: unchanged rendering
    const auto cut=layoutCardTitle(L"Meteonic Debris · 236[B/C]",200,measure);
    assert(cut.split&&!cut.tailEllipsis);
    assert(cut.head==L"Meteonic Debris");
    assert(cut.tail==L" · 236[B/C]");
    assert(cut.headWidth>0&&cut.headWidth<200);
    const auto tiny=layoutCardTitle(L"Meteonic Debris · 236[B/C]",60,measure);
    assert(tiny.split&&tiny.tailEllipsis); // degenerate: still protect suffix
    const auto noCmd=layoutCardTitle(L"Very Long Spell Name Without Command",60,measure);
    assert(!noCmd.split);
}

static void testNativePerHitAttribution() {
    using namespace whm;
    NativeActors actors;
    const auto first=actors.set(0,400);
    const auto second=actors.set(0,400);
    assert(first.id!=second.id); // repeated identical moves are separate casts
    actors.reset();
    const auto third=actors.set(0,400);
    assert(third.id>second.id); // a round never recycles cast IDs
    const auto spell=actors.set(0,614);
    assert(actors.set(0,664).id==spell.id);
    assert(actors.set(0,614).id!=spell.id);
    const auto skill=actors.set(0,505);
    assert(actors.set(0,505,2).id==skill.id);
    assert(!actors.set(0,0).valid());

    constexpr std::uintptr_t own=100,other=200;
    auto birth=[&](std::uintptr_t object,unsigned gen,std::uintptr_t creator,unsigned creatorGen,
                   std::uint64_t cast,int action) {
        NativeEvent e;e.kind=NativeEvent::Birth;e.object=object;e.generation=gen;
        e.owner=e.ally=own;e.creator=creator;e.creatorGeneration=creatorGen;
        e.cast=cast;e.castAction=action;return e;
    };
    auto hit=[&](std::uintptr_t object,unsigned gen,int damage,std::uintptr_t victim=200) {
        NativeEvent e;e.object=object;e.generation=gen;e.owner=e.ally=own;
        e.damage=damage;e.victim=victim;return e;
    };
    NativeAttribution sources;RecordTracker tracker;
    auto feed=[&](std::vector<NativeEvent> events,unsigned frame=1,bool lost=false) {
        auto hits=sources.consume(events,own,lost);
        for(const auto &h:hits)tracker.observeDamage(h.damage,h.action,frame,h.instance,false,0,h.self);
        return hits;
    };
    // Child returns before root; neither retains a persistent parent pointer.
    auto events=std::vector<NativeEvent>{birth(301,1,300,1,0,-1),birth(300,1,own,0,10,400),
        hit(301,1,300),hit(301,1,277),hit(301,1,255)};
    auto b=feed(events);
    assert(b.size()==3&&b[0].instance==b[2].instance);
    assert(tracker.best().actionId==400&&tracker.best().damage==832);
    // JA in the SAME engine update is credited separately, not to B.
    auto ja=hit(own,0,319);ja.boxAction=307;ja.castAction=307;ja.cast=11;
    feed({ja});
    assert(tracker.best().damage==832);
    // Long-delayed scripted self hit: keep its casting spell after idle.
    feed({birth(500,1,own,0,12,614)});
    auto bomb=hit(500,1,5333,own);bomb.ownerAction=0;bomb.path=1;
    feed({bomb},900);
    assert(tracker.best().actionId==614&&tracker.best().damage==5333&&tracker.best().selfDamage);
    // The SAME cast hitting the opponent is a different victim bucket.
    feed({hit(500,1,4000)},901);
    assert(tracker.best().damage==5333);
    // Address reuse, missing chain, owner/credit mismatch and buffer loss
    // never inherit a conveniently similar old move.
    assert(feed({hit(301,2,100)},902)[0].action==-1);
    feed({birth(700,1,701,1,0,-1),birth(701,1,700,1,0,-1)});
    assert(feed({hit(700,1,100)},903)[0].action==-1); // cycle
    auto reflected=hit(500,1,100);reflected.owner=other;
    assert(feed({reflected},904)[0].action==-1);
    assert(feed({hit(500,1,100)},905,true)[0].action==-1);
    // A cached collider from the previous body action is not the new move.
    ja.boxAction=300;
    assert(feed({ja},906)[0].action==-1);
    // Runtime boosts are not rejected by an arbitrary neutral-table cap.
    feed({birth(800,1,own,0,13,505),hit(800,1,9000)},907);
    assert(tracker.best().actionId==505&&tracker.best().damage==9000);
    sources.reset();
    assert(feed({hit(800,1,500)},1)[0].action==-1);
}

// Real 0.5.9 failure: five HIT events were present but Meiling's factory
// wasn't hooked, so every projectile lacked its BIRTH. Capture must precede
// attribution; the game's 1797 combo total is not used as a substitute.
static void testReisenPoisonDirectHPWritesAccumulateToItsSpell() {
    using namespace whm;
    NativeAttribution sources;RecordTracker records;
    records.observeDamage(947,500,1,999);
    NativeEvent root;root.kind=NativeEvent::Birth;root.object=300;root.generation=1;
    root.owner=root.ally=root.creator=100;root.cast=70;root.castAction=608;root.action=858;
    NativeEvent cloud=root;cloud.object=301;cloud.generation=2;cloud.creator=300;cloud.creatorGeneration=1;
    cloud.cast=0;cloud.castAction=-1;
    std::array<NativeEvent,2> births={cloud,root};sources.consume(births,100);
    // Actual trace: 539 individual 2-HP drops, unchanged combo counters.
    int hp=5160;
    for(unsigned tick=0;tick<539;++tick) {
        NativeEvent e;e.object=301;e.generation=2;e.owner=e.ally=100;e.victim=200;
        e.damage=2;e.path=2;e.action=858;e.sequence=3;e.ownerAction=0;
        std::array<NativeEvent,1> batch={e};
        const auto own=sources.consume(batch,100);
        recordNativeFrame(records,own,batch,200,1086+tick,hp,hp-2);hp-=2;
    }
    assert(hp==4082&&records.best().actionId==608&&records.best().damage==1078);
    assert(records.unattributedDamage()==0); // don't double-count HP corroboration
    // An unrelated body hit is still a separate cast.
    NativeDamage normal{500,300,2000,false};NativeEvent direct;
    direct.owner=direct.ally=100;direct.victim=200;direct.damage=500;
    recordNativeFrame(records,std::span<const NativeDamage>(&normal,1),
        std::span<const NativeEvent>(&direct,1),200,1800,hp,hp-500);
    assert(records.best().actionId==608&&records.best().damage==1078);
}

static void testRollbackCancelsPredictedDamageInsteadOfDeduplicatingRealHits() {
    using namespace whm;
    struct State {NativeAttribution sources;RecordTracker record;};
    auto start=[](State &state,int action) {
        NativeEvent born;born.kind=NativeEvent::Birth;born.object=300;
        born.owner=born.ally=born.creator=100;born.cast=1;born.castAction=action;
        state.sources.consume(std::span<const NativeEvent>(&born,1),100);
    };
    auto hit=[](State &state,int action,int amount,unsigned frame) {
        NativeEvent event;event.object=300;event.owner=event.ally=100;event.victim=200;
        event.action=action;event.damage=amount;
        const std::array<NativeEvent,1> events={event};
        const auto own=state.sources.consume(events,100);
        recordNativeFrame(state.record,own,events,200,frame,10000,10000);
    };
    State cirno;start(cirno,410);
    const State beforeVolley=cirno;
    for(int damage:{400,282,265})hit(cirno,410,damage,1210);
    // The abandoned branch really ran, but restoring its checkpoint removes
    // all its contributions. Replay then records the final timeline once.
    cirno=beforeVolley;
    for(int damage:{400,282,265})hit(cirno,410,damage,1212);
    const State beforeFourth=cirno;
    hit(cirno,410,241,1220);cirno=beforeFourth;
    hit(cirno,410,241,1221);hit(cirno,410,225,1235);
    assert(cirno.record.best().damage==1413); // formerly 2601
    State reisen;start(reisen,500);hit(reisen,500,350,1);
    const State beforeSecond=reisen;
    hit(reisen,500,368,2);reisen=beforeSecond;hit(reisen,500,368,3);
    for(int damage:{282,271,261})hit(reisen,500,damage,4);
    assert(reisen.record.best().damage==1532); // formerly 1900
    State remilia;start(remilia,308);const State beforeKick=remilia;
    hit(remilia,308,965,1);remilia=beforeKick;hit(remilia,308,965,2);
    assert(remilia.record.best().damage==965); // formerly 1930
    // Identical legitimate multi-hits, including practice auto-heal, still
    // accumulate. No global event-key blacklist or guessed damage ceiling.
    hit(remilia,308,965,3);
    assert(remilia.record.best().damage==1930);
}

static void testAttributionCheckpointGraphsAreImmutable() {
    using namespace whm;
    NativeAttribution sources;
    NativeEvent born;born.kind=NativeEvent::Birth;born.object=300;
    born.owner=born.creator=100;born.cast=1;born.castAction=410;
    sources.consume(std::span<const NativeEvent>(&born,1),100);
    const auto saved=sources;
    // Replayed physical births may reuse the same address/generation on a
    // different branch. They cannot mutate nodes shared by a saved state.
    born.cast=2;born.castAction=560;
    sources.consume(std::span<const NativeEvent>(&born,1),100);
    NativeEvent hit;hit.object=300;hit.owner=hit.ally=100;hit.victim=200;hit.damage=500;
    auto future=sources.consume(std::span<const NativeEvent>(&hit,1),100);
    assert(future[0].action==560);
    sources=saved;
    const auto restored=sources.consume(std::span<const NativeEvent>(&hit,1),100);
    assert(restored[0].action==410);
}

static void testSuwakoAutomaticRoutesKeepTheirActivationCast() {
    using namespace whm;
    for(const auto [from,to]:{std::pair{305,306},std::pair{552,550},std::pair{553,551}}) {
        NativeActors actors;const auto start=actors.set(0,from,-1,19);
        const auto follow=actors.set(0,to,-1,19);
        assert(follow.id==start.id&&follow.action==from);
        NativeEvent hit;hit.owner=hit.ally=hit.object=100;hit.victim=200;
        hit.ownerCharacter=19;hit.cast=follow.id;hit.castAction=follow.action;
        hit.boxAction=to;hit.damage=200;
        NativeAttribution sources;
        const auto hits=sources.consume(std::span<const NativeEvent>(&hit,1),100);
        assert(hits.size()==1&&hits[0].action==from);
        actors.set(0,0,-1,19);
        assert(actors.set(0,to,-1,19).id!=start.id); // a new input is a new cast
        NativeActors another;const auto other=another.set(0,from,-1,0);
        assert(another.set(0,to,-1,0).id!=other.id); // character-specific only
    }
}

static void testPersistentHelperCommandsDoNotRetagOlderProjectiles() {
    using namespace whm;
    NativeAttribution sources;
    NativeEvent command;command.kind=NativeEvent::Rebind;
    command.object=300;command.owner=command.creator=100;command.cast=1;command.castAction=410;
    NativeEvent old;old.kind=NativeEvent::Birth;old.object=301;old.owner=100;old.creator=300;
    std::array<NativeEvent,2> start={command,old};sources.consume(start,100);
    command.cast=2;command.castAction=560;
    NativeEvent fresh=old;fresh.object=302;
    NativeEvent hitOld;hitOld.object=301;hitOld.owner=hitOld.ally=100;hitOld.victim=200;hitOld.damage=100;
    NativeEvent hitNew=hitOld;hitNew.object=302;hitNew.damage=200;
    // Old hit before the command and old/new children after it retain their
    // own command versions, even when everything occurs in the same frame.
    std::array<NativeEvent,5> next={hitOld,command,fresh,hitOld,hitNew};
    const auto hits=sources.consume(next,100);
    assert(hits.size()==3&&hits[0].action==410&&hits[1].action==410&&hits[2].action==560);
    assert(hits[0].instance==hits[1].instance&&hits[0].instance!=hits[2].instance);
    // A scripted hit can occur inside initialization, before the parent
    // factory returns. Forward references still resolve after batch assembly.
    NativeAttribution initializing;
    NativeEvent parent=command;parent.kind=NativeEvent::Birth;parent.object=400;
    parent.cast=3;parent.castAction=561;
    NativeEvent child=old;child.object=401;child.creator=400;
    NativeEvent earlyHit=hitOld;earlyHit.object=401;
    const std::array<NativeEvent,3> constructors={child,earlyHit,parent};
    const auto initialized=initializing.consume(constructors,100);
    assert(initialized.size()==1&&initialized[0].action==561);
    // An explicit command with missing origin invalidates the helper's NEW
    // output. Earlier descendants remain attributed to their old command.
    command.cast=0;command.castAction=-1;
    fresh.object=303;hitNew.object=303;
    const std::array<NativeEvent,4> unknown={command,fresh,hitOld,hitNew};
    const auto tail=sources.consume(unknown,100);
    assert(tail.size()==2&&tail[0].action==410&&tail[1].action==-1);
}

static void testYuyukoPoisonPersistsAfterOwnerRecoversAndStopsAtOneHP() {
    using namespace whm;
    NativeAttribution sources;RecordTracker records;
    NativeEvent birth;birth.kind=NativeEvent::Birth;birth.object=300;
    birth.owner=birth.ally=birth.creator=100;
    birth.cast=1;birth.castAction=602;birth.action=852;
    sources.consume(std::span<const NativeEvent>(&birth,1),100);
    int hp=1204;
    for(unsigned tick=0;tick<301;++tick) {
        NativeEvent hit;hit.object=300;hit.owner=hit.ally=100;hit.victim=200;
        hit.action=852;hit.ownerAction=0;hit.path=2;
        hit.damage=tick==300?3:4;
        const std::array<NativeEvent,1> events={hit};
        const auto own=sources.consume(events,100);
        recordNativeFrame(records,own,events,200,tick,hp,hp-hit.damage);
        hp-=hit.damage;
    }
    assert(hp==1&&records.best().actionId==602&&records.best().damage==1203);
    assert(records.unattributedDamage()==0);
}

static void testReceivedDamageIsNotLocalUnknownOutput() {
    using namespace whm;
    constexpr std::uintptr_t local=100,opponent=200;
    NativeAttribution sources;RecordTracker records;
    NativeEvent enemyHit;enemyHit.owner=enemyHit.ally=opponent;
    enemyHit.object=opponent;enemyHit.victim=local;enemyHit.damage=1000;
    std::array<NativeEvent,1> events={enemyHit};
    auto own=sources.consume(events,local);
    assert(own.empty());
    recordNativeFrame(records,own,events,opponent,1,10000,10000);
    assert(!records.hasObservedDamage());
    // Physical ownership is not enough: a returned bullet credited to the
    // opponent must not be recorded as our self-inflicted damage.
    events[0].owner=local;events[0].ally=opponent;
    own=sources.consume(events,local);
    assert(own.empty());
    recordNativeFrame(records,own,events,opponent,2,10000,10000);
    assert(!records.hasObservedDamage());
    // The opponent's native self-hit is not our unknown output either.
    events[0]=enemyHit;events[0].victim=opponent;
    own=sources.consume(events,local);
    recordNativeFrame(records,own,events,opponent,3,10000,9000);
    assert(!records.hasObservedDamage());
    // An OWN spell hitting ourselves still records normally, by its event.
    NativeDamage self{4000,614,1,true};
    recordNativeFrame(records,std::span<const NativeDamage>(&self,1),{},opponent,4,10000,10000);
    assert(records.best().actionId==614&&records.best().damage==4000&&records.best().selfDamage);
    // A captured hit plus extra untracked opponent loss in the same frame:
    // count only the residual as unknown; don't drop it or double count.
    records.reset();NativeEvent hit;hit.owner=hit.ally=local;hit.victim=opponent;hit.damage=300;
    NativeDamage named{300,400,2,false};
    recordNativeFrame(records,std::span<const NativeDamage>(&named,1),
        std::span<const NativeEvent>(&hit,1),opponent,5,10000,9000);
    assert(records.best().damage==300&&records.unattributedDamage()==700);
    // Practice refill leaves HP unchanged, but native damage still counts.
    records.reset();
    recordNativeFrame(records,std::span<const NativeDamage>(&named,1),
        std::span<const NativeEvent>(&hit,1),opponent,6,10000,10000);
    assert(records.best().damage==300&&records.unattributedDamage()==0);
}

static void testMeilingFangPoFiveProjectilesOneCast() {
    using namespace whm;
    NativeAttribution sources;RecordTracker record;
    record.observeDamage(800,302,1,900);
    const int damage[]={500,366,336,310,285};
    const unsigned generation[]={90570942,90636487,90702028,90767550,90833095};
    for(unsigned i=0;i<5;++i) {
        NativeEvent birth;birth.kind=NativeEvent::Birth;
        birth.object=300+i%3;birth.generation=generation[i];
        birth.owner=birth.ally=birth.creator=100;
        birth.cast=223;birth.castAction=561;birth.action=825;
        NativeEvent hit=birth;hit.kind=NativeEvent::Hit;hit.victim=200;hit.damage=damage[i];
        const std::array<NativeEvent,2> events={birth,hit};
        for(const auto &h:sources.consume(events,100))
            record.observeDamage(h.damage,h.action,10+i,h.instance);
    }
    assert(record.best().actionId==561&&record.best().damage==1797);
    assert(record.unattributedDamage()==0);
}

static void testArchivedRoundRecordCannotBeRevokedByReusedCastID() {
    whm::RecordTracker r;
    r.observeDamage(1000,400,100,1,false,2000);
    r.observeDamage(100,300,1,1,false,200);
    r.observeDamage(101,300,2,1,false,200);
    assert(r.best().damage==1000&&r.best().actionId==400);
    r.observeDamage(1200,410,3,2,false,1300);
    assert(r.best().damage==1200);
    r.observeDamage(200,410,4,2,false,1300);
    assert(r.best().damage==1000&&r.best().actionId==400);
}

int main() {
    testReisenPoisonDirectHPWritesAccumulateToItsSpell();
    testYuyukoPoisonPersistsAfterOwnerRecoversAndStopsAtOneHP();
    testPersistentHelperCommandsDoNotRetagOlderProjectiles();
    testSuwakoAutomaticRoutesKeepTheirActivationCast();
    testRollbackCancelsPredictedDamageInsteadOfDeduplicatingRealHits();
    testAttributionCheckpointGraphsAreImmutable();
    testReceivedDamageIsNotLocalUnknownOutput();
    testMeilingFangPoFiveProjectilesOneCast();
    testNativePerHitAttribution();
    testArchivedRoundRecordCannotBeRevokedByReusedCastID();
    testPanelLayouts();
    testHudRoundingModes();
    testProjectileRecords();
    testSuikaDelayed5CWithJ6A();
    testDelayedBirthAndDormantProjectileSafety();
    testMixedDamageNeverBecomesBestRecord();
    testSuikaDamageTraceReplay();
    testSuika5CStreamAfterJ6AKeepsItsCast();
    testOverlapFirstSessionNeverBlank();
    testNoBlankBestHitAfterAnyObservedDamage();
    testSpellFollowupAttribution();
    testDamageInferenceSuikaComboReplay();
    testRecordCeilingRejectsImpossibleCast();
    testChargedVariantLabels();
    testEstimatesNeverBecomeActualRecord();
    using namespace whm;
    assert(mergeSkillButtons("623B","623C")=="623B/C");
    assert(mergeSkillButtons("623B","j.623C")=="623B");
    assert(mergeSkillButtons("236B","623C")=="236B");
    static_assert(skillCardForAction(18,505)==104);
    static_assert(skillCardForAction(18,520)==101);
    static_assert(skillCardForAction(4,580)==104);
    static_assert(skillCardForAction(0,580)==-1);
    static_assert(skillCardForAction(18,504)==-1);
    static_assert(cardForAction(18,600)==200);

    // Regression: networking roles are the OPPOSITE of the old implementation.
    // SokuMods DiscordIntegration uses VSCLIENT (4) as local left and
    // Skills can be learned but replaced on the same command slot; the
    // legacy skillMap.notUsed bit and effective level both matter.
    static_assert(skillIndex(17,105)==5);
    static_assert(skillIndex(4,114)==14);
    static_assert(skillIndex(0,112)==-1);
    assert(activeSkillLevel(17,105,false,2,4)==4); // effective lv (疎雨)
    assert(activeSkillLevel(17,105,true,2,2)==-1); // replaced
    assert(activeSkillLevel(17,105,false,-1,2)==-1);
    assert(skillActionForNotation(17,101,"623B")==520);
    assert(skillActionForNotation(17,101,"623C")==521);
    assert(skillActionForNotation(17,101,"j.623B")==522);
    assert(skillActionForNotation(4,109,"421B")==585);
    assert(skillActionForNotation(17,101,"623A")==-1);
    assert(!skillCommandAvailableAtLevel(17,108,"j.214B",0));
    assert(!skillCommandAvailableAtLevel(17,108,"j.214C",1));
    assert(skillCommandAvailableAtLevel(17,108,"j.214B",2));
    assert(skillCommandAvailableAtLevel(17,108,"214B",0));
    // With CH eligibility explicitly sourced, Yellow Sand changes only
    // the first hit, not the rest of a multi-hit move (or unknown shots).
    OpeningCounterCatalog open;
    std::istringstream chData(
        "17|105|623B|-1|1430|https://example.org/meiling\n"
        "4|109|421B|-1|1320|https://example.org/emerald\n");
    assert(open.read(chData)&&open.size()==2);
    assert(open.firstHit(17,105,"623B",4)==1430);
    assert(open.firstHit(4,109,"421B",0)==1320);
    assert(open.firstHit(4,109,"421C",0)==0);
    assert(addOpeningCounterHit(1430,1430,1.,true,true)==1906);
    assert(addOpeningCounterHit(2000,300,1.,true,true)==2100);
    assert(addOpeningCounterHit(2000,300,1.,false,true)==2000);
    assert(addOpeningCounterHit(2000,300,1.,true,false)==2000);
    assert(addOpeningCounterHit(2000,0,1.,true,true)==2000);
    // VSSERVER (5) as local right. A wrong side leaks opponent's hand/skills.
    static_assert(localPlayerSide(4, 1) == 0); // client -> own P1/left
    static_assert(localPlayerSide(5, 1) == 1); // server -> own P2/right
    static_assert(localPlayerSide(8, 0) == 0); // practice
    static_assert(localPlayerSide(2, 0) == 0); // VS COM
    static_assert(localPlayerSide(3, 1) == 0); // offline P1
    static_assert(localPlayerSide(6, 1) == kNoLocalPlayer); // spectator
    static_assert(localPlayerSide(5, 2) == kNoLocalPlayer); // replay
    static_assert(localPlayerSide(4, 2) == kNoLocalPlayer); // replay
    static_assert(localPlayerSide(0, 1) == kNoLocalPlayer); // story
    static_assert(localPlayerSide(99, 1) == kNoLocalPlayer); // unknown
    assert(localPlayerSide(4, 1) != localPlayerSide(5, 1));

    assert(std::abs(gutsFactor(10000) - 1.0) < 0.001);
    assert(std::abs(gutsFactor(0) - 0.7) < 0.001);
    assert(std::abs(gutsFactor(5000) - 0.85) < 0.001);
    Move simple{0, MoveKind::Normal, 300, -1, "5A", {{500, 1., 1., 0}}, true};
    Fighter attacker{}, victim{};
    assert(estimate(simple, attacker, victim).damage == 500);
    attacker.attack = 1.4;
    victim.defense = .8;
    assert(estimate(simple, attacker, victim).damage == 560);
    victim.hp = 0;
    assert(estimate(simple, attacker, victim).damage == 392);
    Move multihit{0, MoveKind::Spell, 200, -1, "testSC", {{1000, .8, 1., 0}, {1000, 1., 1., 0}}, true};
    attacker.attack = 1.0; attacker.spellMultiplier = 1.25;
    victim = {};
    auto e = estimate(multihit, attacker, victim);
    assert(e.valid && e.damage > 2000 && e.complete);
    // Weather should affect only the corresponding attack category.
    Fighter standard{};
    Move singleSpell{0, MoveKind::Spell, 200, -1, "spell", {{1000, 1., 1., 0}}, true};
    Move singleSkill{0, MoveKind::Skill, 500, -1, "skill", {{1000, 1., 1., 0}}, true};
    standard.activeWeather = 1;
    assert(estimate(singleSpell, standard, victim).damage == 1250);
    assert(estimate(singleSkill, standard, victim).damage == 1000);
    standard.activeWeather = 4;
    assert(estimate(singleSpell, standard, victim).damage == 1000);
    assert(estimate(singleSkill, standard, victim).damage == 1250);
    // Runtime effective type multipliers already include Hail/Drizzle.
    // They must NOT be multiplied by 1.25 again by the step engine.
    standard.typeIncludesWeather=true;
    standard.skillMultiplier=1.25;
    assert(estimate(singleSkill, standard, victim).damage == 1250);
    standard.activeWeather=1;standard.spellMultiplier=1.25;
    assert(estimate(singleSpell, standard, victim).damage == 1250);
    standard.activeWeather=4;standard.skillMultiplier=1.25;
    // CH is a separately verified bit on the first hit, not every hit.
    Move chMove{0,MoveKind::Skill,500,2,"sourced",{
        {900,1.,1.,0,true}, {600,1.,1.,0,true}},true};
    Fighter normal{};normal.hp=10000;
    const int noCH=estimate(chMove,normal,normal).damage;
    const int yesCH=estimate(chMove,normal,normal,{4./3.}).damage;
    assert(yesCH>noCH&&yesCH-noCH>=290&&yesCH-noCH<=305);
    chMove.hits[0].counterHitEligible=false;
    assert(estimate(chMove,normal,normal,{4./3.}).damage==noCH);
    chMove.hits[0].counterHitEligible=true;
    // Different current HP changes second-hit guts in the verified engine.
    Fighter lowHP=normal;lowHP.hp=1000;
    assert(estimate(chMove,normal,lowHP,{4./3.}).damage<yesCH);
    // An incomplete profile must never be silently tagged complete.
    singleSpell.verifiedFullHit = false;
    assert(!estimate(singleSpell, standard, victim).complete);
    MoveCatalog db;
    std::istringstream in("#comment\n0|normal|300|-1|5A|1|500:1:1:0\n0|spell|201|-1|foo|0|1000:.9:1:0;500:1:1:0\n");
    std::vector<std::string> err;
    assert(db.read(in, &err));
    assert(db.size() == 2);
    assert(db.find(0, MoveKind::Normal, 300, 2) != nullptr);
    assert(db.find(0, MoveKind::Spell, 201, 0) != nullptr);
    assert(db.find(1, MoveKind::Normal, 300, 0) == nullptr);
    // Optional fifth hit field is a verified 与CH flag, not a guessed
    // ability property. The old four-column profile format stays valid.
    MoveCatalog chCatalog;
    std::istringstream sourcedProfile(
        "4|skill|585|4|Emerald|1|1000:1:1:0:1;500:1:1:0:0\n");
    assert(chCatalog.read(sourcedProfile));
    const Move *chVerified=chCatalog.find(4,MoveKind::Skill,585,4);
    assert(chVerified&&chVerified->verifiedFullHit);
    assert(chVerified->hits.size()==2&&chVerified->hits[0].counterHitEligible);
    assert(!chVerified->hits[1].counterHitEligible);
    assert(estimate(*chVerified,normal,normal,{4./3.}).damage>
           estimate(*chVerified,normal,normal).damage);
    // Published full-move maxima are neutral reference totals; dynamically
    // scaling them is approximate, especially for multi-hit guts/truncation.
    WikiMaxCatalog wiki;
    std::istringstream wsrc("14|spell|207|-1|5851|zenjinrui|https://example.org/tenchi\n");
    assert(wiki.read(wsrc));
    auto *known = wiki.find(14, MoveKind::Spell, 207, -1);
    assert(known != nullptr && known->neutralMax == 5851);
    Fighter a{}, b{};
    assert(rescaleWikiMax(*known, a, b) == 5851);
    a.attack = 1.4;
    b.defense = .8;
    assert(rescaleWikiMax(*known, a, b) == 6553);
    a.activeWeather=1;a.spellMultiplier=1.25;a.typeIncludesWeather=true;
    assert(rescaleWikiMax(*known,a,b)==8191); // no second 1.25
    RecordTracker record;
    record.observe(10000, 9800, 500, 1);
    record.observe(9800, 9400, 500, 2);
    assert(record.best().damage == 600);
    record.observe(9400, 9300, 100, 3);
    assert(record.best().damage == 600);
    assert(record.unattributedDamage() == 100);
    record.observe(9300, 9000, 605, 4);
    assert(record.best().damage == 600);
    record.reset();
    assert(record.best().damage == 0);
    record.observe(10000, 9900, 500, 1);
    for (unsigned frame = 2; frame <= 20; ++frame)
        record.observe(9900, 9900, 500, frame);
    record.observe(9900, 9750, 500, 21);
    assert(record.best().damage == 150);
    // --- Big percentage heat gradient: 100% keeps the existing warm gold.
    const auto baseline = paletteForPercent(100.);
    assert(baseline.core.r == kBaselineNumberPalette.core.r &&
           baseline.core.g == kBaselineNumberPalette.core.g &&
           baseline.core.b == kBaselineNumberPalette.core.b);
    assert(baseline.outline.r == kBaselineNumberPalette.outline.r &&
           baseline.outline.g == kBaselineNumberPalette.outline.g &&
           baseline.outline.b == kBaselineNumberPalette.outline.b);
    assert(baseline.glow.r == kBaselineNumberPalette.glow.r &&
           baseline.glow.g == kBaselineNumberPalette.glow.g &&
           baseline.glow.b == kBaselineNumberPalette.glow.b);
    assert(paletteForPercent(10.).core.b == paletteForPercent(50.).core.b);
    assert(paletteForPercent(400.).outline.r == paletteForPercent(250.).outline.r);
    // Interpolation stays continuous across every node: no step changes.
    for (double p = 50.; p < 250.; p += .25) {
        const auto a = paletteForPercent(p), b = paletteForPercent(p + .25);
        assert(std::abs(int(a.core.r) - int(b.core.r)) <= 4);
        assert(std::abs(int(a.outline.r) - int(b.outline.r)) <= 4);
        assert(std::abs(int(a.glow.g) - int(b.glow.g)) <= 4);
    }
    // 110% sits between gold anchors; 160% between orange-gold and orange-red.
    assert(paletteForPercent(110.).outline.r > paletteForPercent(100.).outline.r);
    assert(paletteForPercent(160.).outline.g < paletteForPercent(150.).outline.g);
    assert(paletteForPercent(std::nan("")).core.r == baseline.core.r);

    // --- Fixed UI strings and notation normalization.
    for (int k = 0; k < int(HudTextKey::KeyCount); ++k) {
        const auto key = static_cast<HudTextKey>(k);
        assert(hudText(key, HudLanguage::Chinese)[0] != '\0');
        assert(hudText(key, HudLanguage::Japanese)[0] != '\0');
        assert(hudText(key, HudLanguage::English)[0] != '\0');
    }
    assert(std::string(hudText(HudTextKey::Strongest, HudLanguage::Chinese)) == "最痛必杀技");
    assert(std::string(hudText(HudTextKey::Record, HudLanguage::Chinese)) == "最痛记录");
    assert(std::string(normalizeNotation("j.2A")) == "J2A");
    assert(std::string(normalizeNotation("f.5A")) == "F5A");
    assert(normalizeNotation("j.236B") == "J236B");
    assert(normalizeNotation("j5A") == "J5A");
    assert(normalizeNotation("J2A") == "J2A"); // idempotent
    assert(normalizeNotation("623B/C") == "623B/C");
    assert(normalizeNotation("Hakurei Amulet") == "Hakurei Amulet");
    assert(std::string(localizeEffectLabel("制御棒", HudLanguage::English)) ==
           "CONTROL RODS");
    assert(localizeEffectLabel("根性补正", HudLanguage::Japanese) ==
           std::string("根性補正"));
    assert(localizeEffectValue("×1.20 另计", HudLanguage::Japanese) == "×1.20 別途");
    assert(weatherName(4, HudLanguage::English) == std::string("Hail"));
    // Regression: the big percentage and the "根性补正" row used different
    // formatters and could show 0.70 vs 0.71 for the same hpFactor.
    char probe[64]{}; std::snprintf(probe, sizeof(probe), "%.2f", 0.705);
    assert(hudFactor(0.705, 2) == probe);
    assert(hudFactor(0.705, 2) == hudFactor(0.705, 2));
    assert(hudFactor(1., 1) == "1.0");
    assert(hudFactor(0.7, 2) == "0.70");
    // Regression: the grey factor and the "根性补正" row share ONE formatted
    // string, so a rounding boundary can never show 0.70 vs 0.71 again.
    assert(gutsLabel(0.705) == std::string("×") + hudFactor(0.705, 2));
    assert(gutsLabel(1.) == "×1.00");

    // Regression: normal attacks keep the source spelling (j.2A) as their
    // "name" while notation is normalized (J2A). The caption must show one
    // ALL-CAPS command, never "j.2A · J2A" / "j6A · J6A".
    assert(sameNotation("j.2A", "J2A"));
    assert(sameNotation("j6A", "J6A"));
    assert(!sameNotation("Hakurei Amulet", "236B"));
    assert(!sameNotation("6A", "6B"));
    assert(cardDisplayName("j.2A", "J2A") == "J2A");
    assert(cardDisplayName("j.6A", "J6A") == "J6A");
    assert(cardDisplayName("j2A", "J2A") == "J2A");
    assert(cardDisplayName("J2A", "J2A") == "J2A");
    assert(cardDisplayName("6A", "6A") == "6A");
    assert(cardDisplayName("j.2A", "") == "J2A");
    assert(cardDisplayName("Hakurei Amulet", "236B") ==
           std::string("Hakurei Amulet · 236B"));
    assert(cardDisplayName("博丽护符", "236B") ==
           std::string("博丽护符 · 236B"));

    // --- Weather text only when effective weather can influence damage.
    assert(weatherLabelVisible(4, false, 1.25, 1.));
    assert(weatherLabelVisible(1, false, 1., 1.25));
    assert(!weatherLabelVisible(0, false, 1., 1.));  // clear sky, no effect
    assert(!weatherLabelVisible(21, false, 1., 1.)); // no weather
    assert(!weatherLabelVisible(4, true, 1.25, 1.));  // sealed
    assert(weatherLabelVisible(7, false, 1.1, 1.));   // unexplained effective buff

    // SokuLib CharacterManager +0x892: only Suika's active 0..480f MPP
    // transformation may switch the strongest panel into basic attacks.
    static_assert(suikaMPPActive(9,480));
    static_assert(suikaMPPActive(9,1));
    static_assert(!suikaMPPActive(9,0));
    static_assert(!suikaMPPActive(9,481));
    static_assert(!suikaMPPActive(0,480));
    static_assert(kSuikaGiantMoves.size()==10);
    const auto mpp=strongestSuikaGiantMove(1.);
    assert(mpp.move && std::string(mpp.move->notation)=="MPP JA");
    assert(mpp.damage==2500);
    assert(strongestSuikaGiantMove(1.4).damage==3500);
    assert(strongestSuikaGiantMove(1.25).damage==3125); // Drizzle
    assert(!strongestSuikaGiantMove(0).move);
    // MPP C's 500-damage shots and 2A/B/C repeated shocks have no
    // verified all-hit totals: rank only the published first contact.
    for(const auto &move:kSuikaGiantMoves) {
        if(std::string(move.notation)=="MPP C") {
            assert(move.multihitIncomplete && move.neutralDamage==2000);
        }
        if(std::string(move.notation)=="MPP 2A/B/C") {
            assert(move.multihitIncomplete && move.neutralDamage==500);
        }
    }
    assert(std::string(hudText(HudTextKey::StrongestNormal,HudLanguage::Chinese))==
           "最痛普通技");
    assert(std::string(hudText(HudTextKey::Strongest,HudLanguage::Chinese))==
           "最痛必杀技");

    // Meiling (character 17): Tiger is a one-hit melee CH modifier,
    // not attackPower, not a blanket special-move multiplier.
    static_assert(meilingFirstHitKind(17,105)==MeilingFirstHitKind::SingleMelee);
    static_assert(meilingFirstHitKind(17,101)==MeilingFirstHitKind::SingleMelee);
    static_assert(meilingFirstHitKind(17,103)==MeilingFirstHitKind::Projectile);
    static_assert(meilingFirstHitKind(17,107)==MeilingFirstHitKind::Projectile);
    static_assert(meilingFirstHitKind(17,104)==MeilingFirstHitKind::MixedOrMultipleMelee);
    static_assert(meilingFirstHitKind(18,105)==MeilingFirstHitKind::Unknown);
    // 1430 = neutral reference for Meiling's Red Energy Release (623B/C).
    // Fierce Tiger (2.0) grants the *first melee hit* forced CH (4/3).
    assert(meilingTigerAdjustedDamage(17,105,1430,1.,true,2.)==3813);
    assert(meilingTigerAdjustedDamage(17,105,1430,1.,false,2.)==1430);
    assert(meilingTigerAdjustedDamage(17,105,1430,1.,true,1.)==1906); // Tiger Lv1
    assert(meilingTigerAdjustedDamage(17,105,1430,1.,true,1.1)==2097); // Tiger Lv2
    assert(meilingTigerAdjustedDamage(17,105,1430,1.,true,1.2)==2288); // Tiger Lv3
    assert(meilingTigerAdjustedDamage(17,105,1430,1.,true,1.3)==2478); // Tiger Lv4
    assert(meilingTigerAdjustedDamage(17,103,1797,1.,true,2.)==1797); // bullets
    assert(meilingTigerAdjustedDamage(17,107,1069,1.,true,2.)==1069); // bullet sphere
    assert(meilingTigerAdjustedDamage(17,104,1500,1.,true,2.)==1500); // unknown first hit
    assert(meilingTigerAdjustedDamage(17,104,1500,1.,true,2.,300)==2000); // ONLY first 300
    assert(meilingTigerAdjustedDamage(18,105,1430,1.,true,2.)==1430);
    assert(meilingTigerAdjustedDamage(17,105,1430,1.,true,0.5)==1430); // invalid scale
    assert(meilingTigerAdjustedDamage(17,105,1430,1.,true,4.)==1430); // invalid scale

    // --- First-hit-only bonus: conservative gating, no whole-move inflation.
    FirstHitSignals ready{1.3, true, true, true, true, false, false, true};
    auto hit = evaluateFirstHit(ready);
    assert(hit.applies && std::abs(hit.multiplier - 1.3) < 1e-9);
    assert(std::abs(commonPercent(1.2, 1., hit) - 156.) < 1e-9);
    assert(std::abs(commonPercent(1.2, 1., {}) - 120.) < 1e-9);
    FirstHitSignals unconfirmed = ready; unconfirmed.sourceConfirmed = false;
    assert(!evaluateFirstHit(unconfirmed).applies);
    FirstHitSignals stale = ready; stale.localMultiplier = 1.;
    assert(!evaluateFirstHit(stale).applies);
    FirstHitSignals wild = ready; wild.localMultiplier = 12.;
    assert(!evaluateFirstHit(wild).applies);
    FirstHitSignals capped = ready; capped.localMultiplier = 8.;
    assert(evaluateFirstHit(capped).multiplier == 4.);
    FirstHitSignals consumed = ready; consumed.firstHitConsumed = true;
    assert(!evaluateFirstHit(consumed).applies);
    FirstHitSignals combo = ready; combo.comboIdle = false;
    assert(!evaluateFirstHit(combo).applies);
    FirstHitSignals hitstun = ready; hitstun.targetOutOfHitstun = false;
    assert(!evaluateFirstHit(hitstun).applies);
    FirstHitSignals projectile = ready; projectile.attackConfirmed = false;
    assert(!evaluateFirstHit(projectile).applies);
    // Regression: armed Tiger Chi Release must show the NEXT first hit
    // while standing idle, not only flash during active hitbox frames.
    FirstHitSignals tigerIdle = ready;
    tigerIdle.attackConfirmed = false;
    tigerIdle.forcedFirstHitArmed = true;
    assert(evaluateFirstHit(tigerIdle).applies);
    tigerIdle.firstHitConsumed = true;
    assert(!evaluateFirstHit(tigerIdle).applies);
    tigerIdle.firstHitConsumed = false;
    tigerIdle.comboIdle = false;
    assert(!evaluateFirstHit(tigerIdle).applies);
    tigerIdle.comboIdle = true;
    tigerIdle.sourceConfirmed = false;
    assert(!evaluateFirstHit(tigerIdle).applies);
    assert(canStartCombo(0,0)); // stale combo/untech counters do not gate idle
    assert(!canStartCombo(50,0));
    assert(!canStartCombo(98,0));
    assert(!canStartCombo(0,1));
    FirstHitSignals tiger=ready;tiger.attackConfirmed=false;
    tiger.forcedFirstHitArmed=true;
    for(int level=1;level<=4;++level) {
        tiger.localMultiplier=1.+.1*(level-1);
        const auto result=evaluateFirstHit(tiger);
        assert(result.applies);
        assert(std::abs(result.multiplier-(4./3.)*(1.+.1*(level-1)))<1e-9);
    }
    tiger.localMultiplier=2.;
    assert(std::abs(evaluateFirstHit(tiger).multiplier-8./3.)<1e-9);
    FirstHitSignals duplicate = ready; duplicate.attackPowerAlreadyIncludes = true;
    assert(!evaluateFirstHit(duplicate).applies);
    assert(std::abs(commonPercent(0., 1., {}) - 100.) < 1e-9);

    std::cout << "All core tests passed\n";
}
