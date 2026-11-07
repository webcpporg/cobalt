// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#ifndef WEBCPP_COBALT_EXAMPLE_ACTIVE_RUN_HPP
#define WEBCPP_COBALT_EXAMPLE_ACTIVE_RUN_HPP

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// The registry belongs to one browser module instance. Each application admits
// one active run; a second registration cannot replace its cancellation target.
EM_JS(int, cobalt_register_run, (const char* key, void* state), {
    const runs =
        (Module.webcppCobaltActiveRuns || (Module.webcppCobaltActiveRuns = Object.create(null)));
    const name = UTF8ToString(key);
    if (runs[name]) {
        return 0;
    }
    runs[name] = state;
    return 1;
});

EM_JS(void, cobalt_unregister_run, (const char* key, void* state), {
    const runs = Module.webcppCobaltActiveRuns;
    const name = UTF8ToString(key);
    if (runs && Object.is(runs[name], state)) {
        delete runs[name];
    }
});

EM_JS(void*, cobalt_find_run, (const char* key), {
    const runs = Module.webcppCobaltActiveRuns;
    return runs ? (runs[UTF8ToString(key)] || 0) : 0;
});
#endif

class active_run {
public:
    active_run([[maybe_unused]] const char* key, [[maybe_unused]] void* state)
#ifdef __EMSCRIPTEN__
        : key_(key),
          state_(state),
          registered_(cobalt_register_run(key, state) != 0)
#endif
    {
    }

    active_run(const active_run&) = delete;
    active_run& operator=(const active_run&) = delete;

    ~active_run() { reset(); }

    [[nodiscard]] bool registered() const { return registered_; }

    void reset() {
        if (registered_) {
#ifdef __EMSCRIPTEN__
            cobalt_unregister_run(key_, state_);
#endif
            registered_ = false;
        }
    }

    static void* find([[maybe_unused]] const char* key) {
#ifdef __EMSCRIPTEN__
        return cobalt_find_run(key);
#else
        return nullptr;
#endif
    }

private:
#ifdef __EMSCRIPTEN__
    const char* key_;
    void* state_;
#endif
    bool registered_ = true;
};

#endif
