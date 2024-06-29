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
        static Value NewString(BaseString val);
        static Value NewSymbol();
        static Value NewSymbolWithDescription(BaseString desc);
        bool IsPersistent();
        bool IsUndefined();
        bool IsNull();
        bool IsString();
        bool IsSymbol();
        Maybe<BaseString> StringGetValue();
        Maybe<NullType> StringSetValue(BaseString val);
        Maybe<vec8> StringGetUtf8Value();
        Maybe<NullType> StringSetUtf8Value(vec8 val);
        Maybe<bool> SymbolHasDescription();
        Maybe<BaseString> SymbolGetDescription();
        Maybe<NullType> SymbolSetDescription(BaseString desc);
    };
    struct NullType {};
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
    struct BaseString {
        vec16 chars;
        std::vector<std::size_t> litchars;
    };
    BaseString utf8ToString(vec8 val);
    vec8 stringToUtf8(BaseString val);
    enum Error {
        ErrorNoError,
        ErrorWrongType,
        ErrorNotFound
    };
    template<typename T>
    struct Maybe {
        Error err;
        T val;
        static Maybe<T> FromNoError(T v);
        static Maybe<T> FromError(Error e);
        bool IsError();
        T ToNoError();
        Error GetError();
    };
}

#define SOL_MUNLOCKRET(m, ret) m.unlock(); return ret;
#define SOL_MUNLOCK(m) m.unlock(); return;

#endif