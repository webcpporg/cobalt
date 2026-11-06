// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Keep the pinned SDK return-value probe distinct from Cobalt's result storage.
import assert from 'node:assert/strict'
import {spawnSync} from 'node:child_process'
import {fileURLToPath} from 'node:url'

assert.equal(process.argv.length, 3)
const driver = fileURLToPath(new URL('../../../asio/test/browser/run.mjs', import.meta.url))
const result = spawnSync(process.execPath, [driver, '--mode', 'workers', process.argv[2]],
    {encoding: 'utf8', timeout: 50000})
assert.ok(!result.error, result.error?.message)
const text = result.stdout + result.stderr
if (result.status === 0) {
    assert.match(text, /browser workers: ok/)
    console.log(text.trim())
    console.log('pthread return: successful raw-return witness in this runtime')
} else {
    assert.match(process.argv[2], /asyncify/)
    assert.match(text, /worker suspended result 42/)
    assert.match(text, /join result 0 value 0/)
    assert.match(text, /"status":1/)
    console.log(text.trim())
    console.log('pthread return: observed Asyncify SDK limitation; use explicit result storage')
}
