#pragma once
#include "NativeDamage.hpp"
#include "RecordTracker.hpp"
#include <algorithm>

namespace whm {
// Only native events prove local damage credit. HP corroboration is limited
// to the opponent's UNEXPLAINED loss, never damage received by the local side.
inline void recordNativeFrame(RecordTracker &record,
                              std::span<const NativeDamage> localHits,
                              std::span<const NativeEvent> allEvents,
                              std::uintptr_t opponent,unsigned frame,
                              int opponentBefore,int opponentAfter) {
    for(const auto &hit:localHits)
        record.observeDamage(hit.damage,hit.action,frame,hit.instance,false,0,hit.self);
    std::int64_t explained=0;
    for(const auto &e:allEvents)
        if(e.kind==NativeEvent::Hit && e.victim==opponent && e.damage>0)
            explained+=e.damage; // includes enemy self-hits and reflected hits
    const auto residual=std::max<std::int64_t>(0,
        static_cast<std::int64_t>(opponentBefore)-opponentAfter-explained);
    if(residual>0)record.observeDamage(static_cast<int>(residual),-1,frame,0,true);
}
}
