// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Exercise the actual example page through Chrome's input and debugging APIs.
import assert from 'node:assert/strict'
import {spawn} from 'node:child_process'
import {createHash} from 'node:crypto'
import {once} from 'node:events'
import {existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs'
import {createServer} from 'node:http'
import {tmpdir} from 'node:os'
import {basename, dirname, extname, join, resolve, sep} from 'node:path'
import {setTimeout as delay} from 'node:timers/promises'
import {fileURLToPath} from 'node:url'
import {findChrome, waitForChrome} from '../../../asio/test/browser/chrome_startup.mjs'

assert.equal(process.argv.length, 3, 'usage: node pages.mjs <program.js>')
const program = resolve(process.argv[2])
const build = dirname(program)
const assets = fileURLToPath(new URL('../../example/browser/', import.meta.url))
const variant = program.includes('asyncify') ? 'asyncify' : 'jspi'
const workers = basename(program).includes('spsc')
const multi = program.includes('threading-multi')
const evidence = join(process.env.WEBCPP_BROWSER_EVIDENCE ?? build, 'page-evidence')
mkdirSync(evidence, {recursive: true})
const profile = mkdtempSync(join(tmpdir(), 'cobalt-pages-'))
let chrome, ws, server
const errors = []
const observations = []
const pending = new Map()
let sequence = 0
const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++sequence
    const timer = setTimeout(() => { pending.delete(id); reject(Error(`CDP timeout: ${method}`)) }, 10000)
    pending.set(id, {resolve, reject, timer})
    ws.send(JSON.stringify({id, method, params}))
})
const evaluate = async expression => {
    const reply = await send('Runtime.evaluate', {expression, returnByValue: true, awaitPromise: true})
    assert.ok(!reply.exceptionDetails, JSON.stringify(reply.exceptionDetails))
    return reply.result.value
}
const wait = async expression => {
    const until = Date.now() + 15000
    while (Date.now() < until) {
        if (await evaluate(expression)) return
        const failure = await evaluate('window.cobaltReport?.error')
        assert.ok(!failure, failure)
        await delay(20)
    }
    throw Error(`Timed out: ${expression}; state=${JSON.stringify(await evaluate('window.cobaltReport'))}`)
}
const click = async selector => {
    const point = await evaluate(`(() => { const r = document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2}; })()`)
    await send('Input.dispatchMouseEvent', {type: 'mousePressed', button: 'left', clickCount: 1, ...point})
    await send('Input.dispatchMouseEvent', {type: 'mouseReleased', button: 'left', clickCount: 1, ...point})
}
const fill = async (selector, value) => {
    if (await evaluate(`document.querySelector(${JSON.stringify(selector)}).disabled`)) return
    await evaluate(`document.querySelector(${JSON.stringify(selector)}).focus(); document.querySelector(${JSON.stringify(selector)}).select()`)
    await send('Input.insertText', {text: String(value)})
}
const choose = async kind => evaluate(`document.querySelector('#kind').value=${JSON.stringify(kind)}; document.querySelector('#kind').dispatchEvent(new Event('change'))`)
const terminal = async status => {
    await wait(`window.cobaltReport?.status === ${JSON.stringify(status)}`)
    assert.deepEqual(errors, [])
    const result = await evaluate('window.cobaltReport')
    assert.equal(await evaluate('document.querySelector("#heartbeat").textContent'),
        `${result.frames} page frames while the program ran.`)
    observations.push(result)
    return result
}

try {
    server = createServer((request, response) => {
        try {
            const requested = decodeURIComponent(new URL(request.url, 'http://localhost').pathname)
            const unisolated = requested.startsWith('/unisolated/')
            const pathname = unisolated ? requested.slice('/unisolated'.length) : requested
            const directory = pathname.startsWith('/build/') ? build : assets
            const relative = pathname.startsWith('/build/') ? pathname.slice(7) : pathname.slice(1)
            const path = resolve(directory, relative || 'index.html')
            if (!path.startsWith(resolve(directory) + sep) || !existsSync(path)) {
                response.writeHead(404).end('Not found'); return
            }
            const type = {'.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
                '.css': 'text/css', '.wasm': 'application/wasm'}[extname(path)] ?? 'application/octet-stream'
            response.writeHead(200, {'Content-Type': type, 'Cache-Control': 'no-store',
                ...(!unisolated ? {'Cross-Origin-Opener-Policy': 'same-origin',
                    'Cross-Origin-Embedder-Policy': 'require-corp'} : {})}).end(readFileSync(path))
        } catch { response.writeHead(500).end() }
    })
    server.listen(0, '127.0.0.1')
    await once(server, 'listening')
    const base = `http://127.0.0.1:${server.address().port}`
    chrome = spawn(findChrome(), ['--headless=new', '--remote-debugging-address=127.0.0.1',
        '--remote-debugging-port=0', `--user-data-dir=${profile}`, '--no-first-run',
        '--disable-component-update', '--disable-background-networking', '--disable-sync',
        '--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE 127.0.0.1', 'about:blank'],
        {stdio: ['ignore', 'ignore', 'pipe']})
    const {devtools, version} = await waitForChrome(chrome, profile)
    const target = await (await fetch(`${devtools}/json/new?about:blank`, {method: 'PUT'})).json()
    ws = new WebSocket(target.webSocketDebuggerUrl)
    ws.addEventListener('message', event => {
        const message = JSON.parse(event.data)
        const entry = pending.get(message.id)
        if (entry) {
            clearTimeout(entry.timer); pending.delete(message.id)
            if (message.error) entry.reject(Error(JSON.stringify(message.error)))
            else entry.resolve(message.result)
        }
        if (message.method === 'Runtime.exceptionThrown') errors.push(message.params.exceptionDetails)
    })
    await new Promise((resolve, reject) => {
        ws.addEventListener('open', resolve, {once: true})
        ws.addEventListener('error', reject, {once: true})
    })
    await send('Page.enable')
    await send('Runtime.enable')
    await send('Emulation.setDeviceMetricsOverride', {width: 1240, height: 900, deviceScaleFactor: 1, mobile: false})
    const page = `${base}/index.html?program=/build/${basename(program)}&variant=${variant}&threads=${multi ? 'multi' : 'single'}&kind=${workers ? 'spsc' : 'timer'}`
    await send('Page.navigate', {url: page})
    await wait('!!document.querySelector("#start")')
    await fill('#count', 0)
    await click('#start')
    await wait('!document.querySelector("#error").hidden')
    assert.equal(await evaluate('document.querySelectorAll("iframe").length'), 0)
    await fill('#count', workers ? 1000 : 4)
    await fill('#delay', 4)
    for (const kind of workers ? ['spsc'] : ['timer', 'custom', 'channel', 'outcome', 'signals']) {
        await choose(kind)
        await click('#start')
        const result = await terminal('completed')
        assert.equal(result.completed, workers ? 1000 : 4)
        assert.equal(result.cancelled, false)
        assert.equal(result.exitStatus, 0)
        if (kind === 'channel') assert.deepEqual(result.values, [0, 1, 2, 3])
        if (kind === 'signals') assert.deepEqual(result.values, [42, 42, 42, 42])
        if (kind === 'outcome') assert.deepEqual(result.values, [11, 11, 11, 11])
        await click('#cancel') // completion wins; disabled cancellation changes nothing
        assert.equal((await evaluate('window.cobaltReport')).status, 'completed')
    }
    await choose(workers ? 'spsc' : 'timer')
    await fill('#count', workers ? 1000000 : 100)
    await fill('#delay', 10)
    await click('#start')
    await wait('window.cobaltReport?.status === "running"')
    const run = await evaluate('window.cobaltReport.run')
    await evaluate('window.oldCobaltFrame = document.querySelector("iframe").contentWindow; document.querySelector("form").requestSubmit()')
    assert.equal(await evaluate('window.cobaltReport.run'), run, 'duplicate submit started another runtime')
    await wait('window.cobaltReport?.completed > 0')
    await click('#cancel')
    const cancelled = await terminal('cancelled')
    assert.ok(cancelled.completed < (workers ? 1000000 : 100))
    assert.ok(cancelled.frames > 0, 'no animation opportunity while pending')
    await fill('#count', workers ? 1000 : 4)
    await fill('#delay', 2)
    await click('#start')
    await evaluate(`window.dispatchEvent(new MessageEvent('message', {origin:location.origin, source:window.oldCobaltFrame, data:{owner:'webcpp-cobalt',run:${run},type:'finished',status:99}}))`)
    await terminal('completed')
    // Keyboard-only start from an input follows the same submission path.
    await fill('#count', workers ? 1000 : 4)
    await send('Input.dispatchKeyEvent', {type: 'keyDown', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13, text: '\r'})
    await send('Input.dispatchKeyEvent', {type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13})
    await wait(`window.cobaltReport.run > ${run + 1}`)
    await terminal('completed')
    for (const [name, width, height] of [['wide', 1240, 900], ['narrow', 390, 844]]) {
        await send('Emulation.setDeviceMetricsOverride', {width, height, deviceScaleFactor: 1, mobile: false})
        await delay(100)
        assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true, `${name} overflow`)
        const screenshot = await send('Page.captureScreenshot', {format: 'png', captureBeyondViewport: true})
        writeFileSync(join(evidence, `${name}.png`), Buffer.from(screenshot.data, 'base64'))
    }
    await fill('#count', workers ? 1000000 : 100)
    await click('#start')
    await wait('window.cobaltReport?.status === "running"')
    await send('Page.navigate', {url: 'about:blank'})
    await delay(100)
    const workersClosed = Date.now() + 5000
    while (Date.now() < workersClosed) {
        const {targetInfos} = await send('Target.getTargets')
        if (!targetInfos.some(target => target.type === 'worker' && target.url.startsWith(base))) break
        await delay(50)
    }
    assert.ok(!(await send('Target.getTargets')).targetInfos.some(
        target => target.type === 'worker' && target.url.startsWith(base)), 'a worker survived page unload')
    // Actual response headers, rather than a mocked crossOriginIsolated value.
    if (multi) {
        await send('Page.navigate', {url: page.replace('/index.html?', '/unisolated/index.html?')})
        await wait('!!document.querySelector("#start")')
        assert.equal(await evaluate('crossOriginIsolated'), false)
        await click('#start')
        await wait('!document.querySelector("#error").hidden')
        assert.match(await evaluate('document.querySelector("#error").textContent'), /COOP and COEP/)
        assert.equal(await evaluate('document.querySelectorAll("iframe").length'), 0)
    }
    if (variant === 'jspi') {
        await send('Page.navigate', {url: page})
        await wait('!!document.querySelector("#start")')
        await evaluate('delete WebAssembly.Suspending')
        await click('#start')
        await wait('!document.querySelector("#error").hidden')
        assert.match(await evaluate('document.querySelector("#error").textContent'), /does not support JSPI/)
        assert.equal(await evaluate('document.querySelectorAll("iframe").length'), 0)
    }
    assert.deepEqual(errors, [])
    const hash = path => createHash('sha256').update(readFileSync(path)).digest('hex')
    writeFileSync(join(evidence, 'observations.json'), JSON.stringify({browser: version.Browser, variant,
        threading: multi ? 'multi' : 'single', program, sha256: hash(program),
        wasmSha256: hash(program.replace(/\.js$/, '.wasm')), observations}, null, 2) + '\n')
    console.log(`PASS browser pages: ${version.Browser}, ${variant}, ${multi ? 'multi' : 'single'}, ${observations.length} checked runs`)
} finally {
    for (const entry of pending.values()) { clearTimeout(entry.timer); entry.reject(Error('driver closing')) }
    pending.clear()
    if (ws) ws.close()
    if (chrome && chrome.exitCode === null && chrome.signalCode === null) {
        const ended = once(chrome, 'exit')
        const timer = setTimeout(() => chrome.kill('SIGKILL'), 5000)
        chrome.kill()
        try { await ended } finally { clearTimeout(timer) }
    }
    if (server) { server.closeAllConnections(); server.close() }
    rmSync(profile, {recursive: true, force: true})
}
