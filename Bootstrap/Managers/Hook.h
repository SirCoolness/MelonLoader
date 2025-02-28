#pragma once
#include <vector>
#include <unordered_map>
#ifdef __ANDROID__
#include <funchook.h>
#endif

class Hook
{
public:
    static void Attach(void** target, void* detour);
    static void Detach(void** target, void* detour);
#ifdef __ANDROID__
    static bool FunchookPrepare();
#endif
#ifdef __ANDROID__
private:
    struct FunchookDef {
        funchook_t* handle;
        void* original;
    };
    static std::unordered_map<void*, FunchookDef*> HookMap;
#endif
};
