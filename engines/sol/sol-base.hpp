#ifndef SOL_ENGINE_BASE
#define SOL_ENGINE_BASE

#include <mutex>
#include <thread>
#include <vector>
#include <cinttypes>
#include <functional>
#include <string>

namespace sol {
    // Aliases for commonly used types
    using voidfn = std::function<void()>;
    using vec8 = std::vector<uint8_t>;
    using vec16 = std::vector<uint16_t>;
    struct Value;
    struct Thread;
    struct InitsManager;
    struct BaseString;
    struct NullType;
    // An error for when a `Maybe` represents an error
    enum Error {
        ErrorNoError,
        ErrorWrongType,
        ErrorNotFound
    };
    // A value that can be either an error or an a value of type `T`
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
    // The type of all JS Values
    struct Value {
        void* _;
        // Should not be used outside Sol's internals
        void CoreSet(vec8 key, void* val);
        // Should not be used outside Sol's internals
        void* CoreGet(vec8 key);
        // Should not be used outside Sol's internals
        bool CoreHas(vec8 key);
        // Creates a new `Value` with the value of JS's `undefined`
        static Value NewUndefined();
        // Creates a copy of this `Value`
        Value Copy();
        // Garbage collects this `Value`
        void Collect();
        // Creates a new `Value` with the value of JS's `null`
        static Value NewNull();
        // Makes this `Value` not persistent, meaning it will be garbage collected when other values need it
        void MakeNotPersistent();
        // Makes this `Value` persistent, meaning it won't be garbage collected automatically, which is the default when you create new `Values`
        void MakePersistent();
        // Creates a new `Value` with its value being a JS string having the value `val`
        static Value NewString(BaseString val);
        // Creates a new `Value` with its value being a JS symbol with no description
        static Value NewSymbol();
        // Creates a new `Value` with its value being a JS symbol with the description `desc`
        static Value NewSymbolWithDescription(BaseString desc);
        // Returns whether this `Value` is persistent
        bool IsPersistent();
        // Returns whether this `Value` has the value of `undefined`
        bool IsUndefined();
        // Returns whether this `Value` has the value of `null`
        bool IsNull();
        // Returns whether this `Value` is a JS string
        bool IsString();
        // Returns whether this `Value` is a JS symbol
        bool IsSymbol();
        // Returns the string's contents if this `Value` is a string, otherwise returns `ErrorWrongType`
        Maybe<BaseString> StringGetValue();
        // Sets the string's contents to `val` if this `Value` is a string, otherwise returns `ErrorWrongType`
        Maybe<NullType> StringSetValue(BaseString val);
        // Equivalent to `StringGetValue`ing, asserting that no `Error` occured, and `stringToUtf8`ing the result
        Maybe<vec8> StringGetUtf8Value();
        // Equivalent to `utf8ToString`ing `val`, and `StringSetValue`ing the result
        Maybe<NullType> StringSetUtf8Value(vec8 val);
        // Returns whether the symbol has a description if this `Value` is a symbol, otherwise returns `ErrorWrongType`
        Maybe<bool> SymbolHasDescription();
        // Returns the symbol's description if there is any, otherwise returning `ErrorNotFound`. If this `Value` isn't a symbol, it retuns `ErrorWrongType`
        Maybe<BaseString> SymbolGetDescription();
        // Sets the symbol's description to `desc` if this `Value` is a symbol, otherwise returns `ErrorWrongType`
        Maybe<NullType> SymbolSetDescription(BaseString desc);
    };
    // A void type that works for `Maybe`s
    struct NullType {};
    // Inits Sol. You should do this before starting to use Sol
    void Init();
    // Deinits Sol. Do this when you finished doing what you wanted to do with Sol, generally before the program exits
    void Teardown();
    // A thread in a way you can manage it
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
    // A class to manage your inits and deinits
    struct InitsManager {
        std::vector<voidfn> deiniters;
        std::vector<std::pair<voidfn, voidfn>> initLaters;
        std::mutex m;
        void AddInitNow(voidfn initer, voidfn deiniter);
        void AddInitLater(voidfn initer, voidfn deiniter);
        void InitLaters();
        void Deinit();
    };
    // An inits manager for initializing and deinitializing Sol	
    InitsManager GlobalIM;
    // Spawns a thread using the `Thread` for the current thread, won't work if this thread doesn't have a `Thread`
    Thread* SpawnThread(voidfn code);
    // Convert a `char*` to an array of bytes
    vec8 cstringToVec8(char* cstr);
    // Compare two arrays of bytes for equality
    bool vec8Compare(vec8 v1, vec8 v2);
    // Converts an array of bytes into an `std::string`
    std::string vec8ToStdString(vec8 v);
    // A UTF-16 string
    struct BaseString {
        vec16 chars;
        std::vector<std::size_t> litchars;
    };
    // Convert an array of UTF-8 bytes to a UTF-16 `BaseString`
    BaseString utf8ToString(vec8 val);
    // Converts an UTF-16 `BaseString` to an array of UTF-8 bytes
    vec8 stringToUtf8(BaseString val);
}

// Equivalent to unlocking `m` (a `std::mutex`) and returning `ret`
#define SOL_MUNLOCKRET(m, ret) m.unlock(); return ret;
// Equivalent to unlocking `m` (a `std::mutex`) and returning
#define SOL_MUNLOCK(m) m.unlock(); return;

#endif