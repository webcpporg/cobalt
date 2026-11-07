// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#ifndef WEBCPP_COBALT_EXAMPLE_EVENTS_HPP
#define WEBCPP_COBALT_EXAMPLE_EVENTS_HPP

// Example-host boundary. Call only on the creator/page thread, including when
// the operation itself runs on a worker. The CLI host has no event observer.
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, cobalt_event, (const char* type, int count, int value, int cancelled), {
    if (Module.onCobaltEvent) {
        Module.onCobaltEvent(
            {type : UTF8ToString(type), count, value, cancelled : Boolean(cancelled)});
    }
});
#else
static void cobalt_event([[maybe_unused]] const char* type, [[maybe_unused]] int count,
                         [[maybe_unused]] int value, [[maybe_unused]] int cancelled) {}
#endif

#endif
