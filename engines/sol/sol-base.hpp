#ifndef SOL_ENGINE_BASE
#define SOL_ENGINE_BASE

#include <mutex>
#include <thread>
#include <vector>
#include <cinttypes>
#include <functional>
#include <string>

namespace sol {
    using voidfn = std::function<void()>;
    using vec8 = std::vector<uint8_t>;
    using vec16 = std::vector<uint16_t>;
    struct Value {
        void* _;
        void CoreSet(vec8 key, void* val);
        void* CoreGet(vec8 key);
        bool CoreHas(vec8 key);
        static Value NewUndefined();
        Value Copy();
        void Collect();
        static Value NewNull();
        void MakeNotPersistent();
        void MakePersistent();
        static Value NewString(vec16 val);
    };
    void Init();
    void Teardown();
    struct Thread {
        std::thread t;
        std::thread w;
        std::mutex tm;
        std::mutex wm;
        std::vector<Thread*> st;
        static Thread* New(voidfn code);
        Thread* Spawn(voidfn code);
        void Wait();
    };
    struct InitsManager {
        std::vector<voidfn> deiniters;
        std::vector<std::pair<voidfn, voidfn>> initLaters;
        std::mutex m;
        void AddInitNow(voidfn initer, voidfn deiniter);
        void AddInitLater(voidfn initer, voidfn deiniter);
        void InitLaters();
        void Deinit();
    };
    InitsManager GlobalIM;
    Thread* SpawnThread(voidfn code);
    vec8 cstringToVec8(char* cstr);
    bool vec8Compare(vec8 v1, vec8 v2);
    std::string vec8ToStdString(vec8 v);
    vec16 utf8To16(vec8 val);
    vec8 utf16To8(vec16 val);
}

#define SOL_MUNLOCKRET(m, ret) m.unlock(); return ret;
#define SOL_MUNLOCK(m) m.unlock(); return;

#endif