// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Execute namespace-only programs in fresh browser pages and dedicated workers.
import assert from 'node:assert/strict'
import {spawn} from 'node:child_process'
import {createHash} from 'node:crypto'
import {once} from 'node:events'
import {mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs'
import {createServer} from 'node:http'
import {tmpdir} from 'node:os'
import {dirname, extname, join, resolve, sep} from 'node:path'
import {setTimeout as delay} from 'node:timers/promises'
import {findChrome, waitForChrome} from '../../../asio/test/browser/chrome_startup.mjs'

assert.equal(process.argv.length, 4, 'usage: node originals.mjs cases.json output.json')
const cases = JSON.parse(readFileSync(process.argv[2], 'utf8'))
const root = resolve(cases.root)
const destination = resolve(process.argv[3])
const profile = mkdtempSync(join(tmpdir(), 'cobalt-originals-'))
const hash = path => createHash('sha256').update(readFileSync(path)).digest('hex')
const observations = []
let chrome, server, ws
const pending = new Map()
let sequence = 0
const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++sequence
    const timer = setTimeout(() => { pending.delete(id); reject(Error(method + ' timed out')) }, 15000)
    pending.set(id, {resolve, reject, timer})
    ws.send(JSON.stringify({id, method, params}))
})
const bootstrap = `
const query = new URLSearchParams(location.search)
const url = query.get('program')
const result = {stdout:[],stderr:[],status:null,error:null}
const report = () => {
    if (typeof document === 'undefined') postMessage(result)
    else window.report = result
}
self.Module = {
    arguments: JSON.parse(query.get('args')),
    locateFile: name => new URL(name, url).href,
    print: line => result.stdout.push(line),
    printErr: line => result.stderr.push(line),
    onExit: status => { result.status = status; report() },
    onAbort: error => { result.error = String(error); report() }
}
if (query.get('threaded') === 'true') self.Module.mainScriptUrlOrBlob = url
addEventListener('error', event => { result.error = event.message; report() })
addEventListener('unhandledrejection', event => { result.error = String(event.reason); report() })
if (typeof document === 'undefined') importScripts(url)
else { const script = document.createElement('script'); script.src = url; document.body.appendChild(script) }
`
try {
    server = createServer((request, response) => {
        const url = new URL(request.url, 'http://localhost')
        const headers = {'Cache-Control':'no-store', 'Cross-Origin-Opener-Policy':'same-origin',
            'Cross-Origin-Embedder-Policy':'require-corp'}
        if (url.pathname === '/runner.js') {
            response.writeHead(200, {...headers, 'Content-Type':'text/javascript'}).end(bootstrap)
        } else if (url.pathname === '/run') {
            const script = url.searchParams.get('host') === 'worker' ?
                `const worker = new Worker('/runner.js'+location.search); worker.onmessage=e=>window.report=e.data; worker.onerror=e=>window.report={error:e.message};` : bootstrap
            response.writeHead(200, {...headers, 'Content-Type':'text/html'}).end(
                '<!doctype html><meta charset="utf-8"><title>Cobalt source witness</title><body><script>' + script + '</script>')
        } else {
            try {
                const path = resolve(root, decodeURIComponent(url.pathname).slice(1))
                assert.ok(path.startsWith(root + sep))
                const type = extname(path) === '.wasm' ? 'application/wasm' : 'text/javascript'
                response.writeHead(200, {...headers, 'Content-Type':type}).end(readFileSync(path))
            } catch { response.writeHead(404).end() }
        }
    })
    server.listen(0, '127.0.0.1')
    await once(server, 'listening')
    const base = `http://127.0.0.1:${server.address().port}`
    chrome = spawn(findChrome(), ['--headless=new', '--remote-debugging-port=0',
        '--remote-debugging-address=127.0.0.1', `--user-data-dir=${profile}`, '--no-first-run',
        '--disable-component-update', '--disable-background-networking', '--disable-sync',
        '--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE 127.0.0.1', 'about:blank'],
        {stdio:['ignore','ignore','pipe']})
    const {devtools, version} = await waitForChrome(chrome, profile)
    for (const item of cases.programs) {
        for (const host of ['page', 'worker']) {
            const target = await (await fetch(`${devtools}/json/new?about:blank`, {method:'PUT'})).json()
            ws = new WebSocket(target.webSocketDebuggerUrl)
            ws.addEventListener('message', event => {
                const message = JSON.parse(event.data)
                const entry = pending.get(message.id)
                if (entry) {
                    clearTimeout(entry.timer); pending.delete(message.id)
                    if (message.error) entry.reject(Error(JSON.stringify(message.error)))
                    else entry.resolve(message.result)
                }
            })
            await new Promise((resolve, reject) => {
                ws.addEventListener('open', resolve, {once:true})
                ws.addEventListener('error', reject, {once:true})
            })
            const query = new URLSearchParams({program:base+'/'+item.program,
                args:JSON.stringify(item.args ?? []), host,
                threaded:String(item.program.includes('threading-multi'))})
            await send('Page.navigate', {url:base+'/run?'+query})
            const deadline = Date.now() + 30000
            let result
            while (Date.now() < deadline) {
                const reply = await send('Runtime.evaluate', {expression:'window.report', returnByValue:true})
                result = reply.result.value
                if (result) break
                await delay(20)
            }
            assert.ok(result, `${item.id}/${host}: no completion`)
            assert.equal(result.error, null, JSON.stringify(result))
            assert.equal(result.status, 0, JSON.stringify(result))
            assert.ok(result.stderr.includes('webcpp: upstream application reached normal exit'), JSON.stringify(result))
            if (item.application === 'channel') assert.deepEqual(result.stdout, ['0','1','2','3'])
            if (item.application === 'thread' || item.application === 'thread_pool') {
                const sums = result.stdout.map(line => line.match(/adding (-?\d+) to (-?\d+), is (-?\d+)/)).filter(Boolean)
                assert.equal(sums.length, 33)
                for (const [,left,right,value] of sums) assert.equal(+value, +left + +right)
            }
            observations.push({...item, host, browser:version.Browser, ...result,
                js_sha256:hash(join(root,item.program)), wasm_sha256:hash(join(root,item.program.replace(/\.js$/,'.wasm')))})
            mkdirSync(dirname(destination), {recursive:true})
            writeFileSync(destination, JSON.stringify({observations}, null, 2)+'\n')
            console.log(`PASS ${item.id}/${host}: ${version.Browser}`)
            ws.close(); ws = null
            await fetch(`${devtools}/json/close/${target.id}`)
        }
    }
} finally {
    for (const entry of pending.values()) clearTimeout(entry.timer)
    if (ws) ws.close()
    if (chrome && chrome.exitCode === null && chrome.signalCode === null) {
        const exited = once(chrome, 'exit')
        const timer = setTimeout(() => chrome.kill('SIGKILL'), 5000)
        chrome.kill()
        try { await exited } finally { clearTimeout(timer) }
    }
    if (server) { server.closeAllConnections(); server.close() }
    rmSync(profile, {recursive:true, force:true})
}
