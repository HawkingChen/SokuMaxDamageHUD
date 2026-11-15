#pragma once
#include <windows.h>
#include <cstdint>

namespace whm {
// Giuroll 0.6.18's public addRollbackCb ABI (Hagb/giuroll-hagb,
// src/lib.rs::Callbacks). Restore-pre runs before game memory is restored;
// restore-post runs afterwards. Both confirmed and discarded states are freed.
struct GiurollCallbacks {
    std::uint32_t (__cdecl *save)();
    void (__cdecl *loadPre)(std::uintptr_t,std::uint32_t);
    void (__cdecl *loadPost)(std::uint32_t);
    void (__cdecl *free)(std::uint32_t,bool);
};
static_assert(sizeof(GiurollCallbacks)==4*sizeof(void*));

template<class Bridge> struct GiurollBridge {
    using Snapshot=typename Bridge::Snapshot;
    static std::uint32_t __cdecl save() noexcept {
        static_assert(sizeof(void*)==4,"Giuroll callbacks use Win32 handles");
        try {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
                new Snapshot(Bridge::capture())));
        } catch(...) { Bridge::failed();return 0; }
    }
    static void __cdecl loadPre(std::uintptr_t frame,std::uint32_t handle) noexcept {
        if(!handle){Bridge::failed();return;}
        try { Bridge::restorePre(*reinterpret_cast<const Snapshot*>(handle),frame); }
        catch(...) { Bridge::failed(); }
    }
    static void __cdecl loadPost(std::uint32_t handle) noexcept {
        if(!handle)return;
        try { Bridge::restorePost(*reinterpret_cast<const Snapshot*>(handle)); }
        catch(...) { Bridge::failed(); }
    }
    static void __cdecl free(std::uint32_t handle,bool) noexcept {
        delete reinterpret_cast<Snapshot*>(handle);
    }
    static inline const GiurollCallbacks callbacks{save,loadPre,loadPost,free};
    static bool attach(HMODULE module=nullptr) {
        if(!module)module=GetModuleHandleW(L"giuroll.dll");
        if(!module)return false;
        using Add=void (__cdecl *)(const GiurollCallbacks*);
        const auto add=reinterpret_cast<Add>(GetProcAddress(module,"addRollbackCb"));
        if(!add)return false;
        add(&callbacks);return true;
    }
};
}
