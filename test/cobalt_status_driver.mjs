// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// A nonzero exit alone is insufficient: traps and unfinished main also fail.
import assert from 'node:assert/strict'
import {spawnSync} from 'node:child_process'
import {extname, resolve} from 'node:path'

const program = resolve(process.argv.at(-1))
const wasm = extname(program) === '.wasm'
const command = wasm ? (process.env.WASMTIME || 'wasmtime') :
    extname(program) === '.js' ? process.execPath : program
const args = wasm ? ['run', '-S', 'p3', '-W', 'component-model-async=y',
    '-W', 'component-model-threading=y', '-W', 'timeout=8s', program] :
    command === program ? [] : [program]
// EXIT_RUNTIME=0 retains the runtime. Debug Asyncify emits this exact notice.
const notice = status => `program exited (with status: ${status}), but keepRuntimeAlive() ` +
    'is set (counter=0) due to an async operation, so halting execution but not exiting ' +
    'the runtime or preventing further async execution (you can use emscripten_force_exit, ' +
    'if you want to force a true shutdown)\n'
for (const status of [7, 0]) {
    const extra = status === 0 ? ['zero'] : []
    const result = spawnSync(command, [...args, ...extra], {encoding: 'utf8', timeout: 10000})
    assert.ifError(result.error)
    assert.equal(result.signal, null, result.stderr)
    const expected = wasm && status !== 0 ? 1 : status
    assert.equal(result.status, expected, result.stdout + result.stderr)
    assert.equal(result.stdout.trim(), `cobalt: returning ${status}`)
    assert.ok(result.stderr === '' || result.stderr === notice(status), result.stderr)
    console.log(`cobalt: return ${status}, exact host exit status ${expected}`)
}
