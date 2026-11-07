#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Bounded observations of the exact upstream native Python binding."""
import asyncio
import gc
import json
import sys

import importlib

cobalt = importlib.import_module("boost_cobalt_example_python")


async def main():
    mode = sys.argv[1]
    problems = []
    loop = asyncio.get_running_loop()
    loop.set_exception_handler(
        lambda _, context: problems.append(
            {"type": type(context.get("exception")).__name__, "message": context.get("message")}
        )
    )
    if mode == "values":
        values = [value async for value in cobalt.test_generator()]
        answer = await cobalt.test_promise()
        print(json.dumps({"generator": values, "promise": answer}))
    elif mode == "error":

        async def fail():
            raise ValueError("python error witness")

        try:
            await asyncio.wait_for(cobalt.test_py_promise(fail()), 1)
        except BaseException as error:
            print(
                json.dumps(
                    {"caught": type(error).__name__, "message": str(error), "loop_errors": problems}
                )
            )
        else:
            raise AssertionError("Python error was lost")
    elif mode == "cancel-outer":
        pending = asyncio.ensure_future(cobalt.test_promise())
        await asyncio.sleep(0.01)
        pending.cancel()
        try:
            await pending
        except asyncio.CancelledError:
            pass
        else:
            raise AssertionError("Cancelled future completed normally")
        # The upstream timer expires after 100 ms. Keep the loop alive to detect
        # a late write into the already-cancelled Python future.
        await asyncio.sleep(0.01)
        if hasattr(cobalt, "active_coroutines"):
            assert cobalt.active_coroutines() == 0, "C++ kept waiting after Python cancelled"
        await asyncio.sleep(0.2)
        print(json.dumps({"cancelled": pending.cancelled(), "loop_errors": problems}))
    elif mode == "cancel-inner":

        async def cancel_self():
            current = asyncio.current_task()
            assert current is not None
            current.cancel()
            await asyncio.sleep(0)

        try:
            await asyncio.wait_for(cobalt.test_py_promise(cancel_self()), 0.5)
        except BaseException as error:
            print(json.dumps({"caught": type(error).__name__, "loop_errors": problems}))
        else:
            raise AssertionError("Inner cancellation was lost")
    elif mode == "close-generator":
        generator = cobalt.test_generator()
        iterator = generator.__aiter__()
        assert await anext(iterator) == 1
        await iterator.aclose()
        del iterator, generator
        gc.collect()
        print(json.dumps({"closed": True}))
    else:
        raise AssertionError(mode)
    await asyncio.sleep(0)
    if hasattr(cobalt, "active_coroutines"):
        assert cobalt.active_coroutines() == 0, "a C++ coroutine frame survived the operation"


if sys.argv[1] == "repeat":
    sys.argv[1] = "values"
    asyncio.run(main())
    gc.collect()
    asyncio.run(main())
else:
    asyncio.run(main())
