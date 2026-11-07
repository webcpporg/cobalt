// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Explicit buffered-loader experiment. Fetch, compile and instantiate are timed
// separately; this does not claim default streaming-loader startup performance.
import assert from 'node:assert/strict'
import {spawn} from 'node:child_process'
import {createHash} from 'node:crypto'
import {once} from 'node:events'
import {appendFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync} from 'node:fs'
import {createServer} from 'node:http'
import {tmpdir} from 'node:os'
import {dirname, extname, join, resolve, sep} from 'node:path'
import {setTimeout as delay} from 'node:timers/promises'
import {findChrome, waitForChrome} from '../../asio/test/browser/chrome_startup.mjs'

assert.equal(process.argv.length, 4, 'usage: node browser.mjs cases.json samples.jsonl')
const experiment = JSON.parse(readFileSync(process.argv[2], 'utf8'))
const root = resolve(experiment.root)
const output = resolve(process.argv[3])
mkdirSync(dirname(output), {recursive:true})
const completed = new Set(existsSync(output) ? readFileSync(output, 'utf8').trim().split('\n')
    .filter(Boolean).map(line => JSON.parse(line)).filter(row => row.status === 'passed').map(row => row.id) : [])
const profile = mkdtempSync(join(tmpdir(), 'webcpp-benchmark-'))
const pending = new Map()
let sequence = 0, chrome, server, ws
const send = (method, params={}) => new Promise((resolve, reject) => {
    const id = ++sequence
    const timer = setTimeout(() => { pending.delete(id); reject(Error(method+' timed out')) }, 15000)
    pending.set(id, {resolve,reject,timer})
    ws.send(JSON.stringify({id,method,params}))
})
const bootstrap = `
const query = new URLSearchParams(location.search)
const url = query.get('program')
const started = performance.now()
const result = {stdout:[],stderr:[],status:null,error:null,loader:'buffered-explicit',
    clock:'performance.now',crossOriginIsolated,fetch_ms:null,compile_ms:null,instantiate_ms:null,
    runtime_ready_ms:null,complete_ms:null,frame_delays:[],long_tasks:[]}
let previous = null
if (typeof document !== 'undefined') {
    const frame = now => {if (previous !== null) result.frame_delays.push(now-previous);previous=now;requestAnimationFrame(frame)}
    requestAnimationFrame(frame)
    new PerformanceObserver(list=>result.long_tasks.push(...list.getEntries().map(e=>e.duration)))
        .observe({entryTypes:['longtask']})
}
const report = () => {
    result.complete_ms=performance.now()-started
    if (typeof document === 'undefined') postMessage(result)
    else window.report=result
}
const instantiate = WebAssembly.instantiate.bind(WebAssembly)
const compile = WebAssembly.compile.bind(WebAssembly)
WebAssembly.instantiate = async (bytes,imports) => {
    if (bytes instanceof WebAssembly.Module) return instantiate(bytes,imports)
    const a=performance.now(); const module=await compile(bytes); const b=performance.now()
    const instance=await instantiate(module,imports); const c=performance.now()
    result.compile_ms=b-a; result.instantiate_ms=c-b
    return {module,instance}
}
WebAssembly.instantiateStreaming = async (response,imports) => {
    const a=performance.now(); const bytes=await (await response).arrayBuffer()
    result.fetch_ms=performance.now()-a
    return WebAssembly.instantiate(bytes,imports)
}
self.Module={arguments:JSON.parse(query.get('args')),locateFile:name=>new URL(name,url).href,
    print:line=>result.stdout.push(line),printErr:line=>result.stderr.push(line),
    onRuntimeInitialized:()=>{result.runtime_ready_ms=performance.now()-started},
    onExit:status=>{result.status=status;report()},onAbort:error=>{result.error=String(error);report()}}
if (query.get('threaded')==='true') self.Module.mainScriptUrlOrBlob=url
addEventListener('error',event=>{result.error=event.message;report()})
addEventListener('unhandledrejection',event=>{result.error=String(event.reason);report()})
if (typeof document === 'undefined') importScripts(url)
else {const script=document.createElement('script');script.src=url;document.body.appendChild(script)}
`
let failures = 0
const verified = new Set()
try {
    server = createServer((request,response) => {
        const url=new URL(request.url,'http://localhost')
        const headers={'Cache-Control':'no-store','Cross-Origin-Opener-Policy':'same-origin',
            'Cross-Origin-Embedder-Policy':'require-corp'}
        if (url.pathname==='/runner.js') {
            response.writeHead(200,{...headers,'Content-Type':'text/javascript'}).end(bootstrap)
        } else if (url.pathname==='/run') {
            const code=url.searchParams.get('host')==='worker' ?
                `const worker=new Worker('/runner.js'+location.search);worker.onmessage=e=>window.report=e.data;worker.onerror=e=>window.report={error:e.message};` : bootstrap
            response.writeHead(200,{...headers,'Content-Type':'text/html'}).end(
                '<!doctype html><meta charset="utf-8"><title>WebCPP measurement</title><body><script>'+code+'</script>')
        } else {
            try {
                const path=resolve(root,decodeURIComponent(url.pathname).slice(1))
                assert.ok(path.startsWith(root+sep))
                response.writeHead(200,{...headers,'Content-Type':extname(path)==='.wasm'?'application/wasm':'text/javascript'})
                    .end(readFileSync(path))
            } catch {response.writeHead(404).end()}
        }
    })
    server.listen(0,'127.0.0.1'); await once(server,'listening')
    const base=`http://127.0.0.1:${server.address().port}`
    const browserFlags=['--headless=new','--remote-debugging-port=0',
        '--remote-debugging-address=127.0.0.1',`--user-data-dir=${profile}`,'--no-first-run',
        '--disable-component-update','--disable-background-networking','--disable-sync',
        '--disable-background-timer-throttling','--disable-renderer-backgrounding','about:blank']
    chrome=spawn(findChrome(),browserFlags,{stdio:['ignore','ignore','pipe']})
    const {devtools,version}=await waitForChrome(chrome,profile)
    for (const item of experiment.cases) {
        if (completed.has(item.id)) continue
        for (const artifact of item.artifact_files ?? []) {
            const key = artifact.path + ':' + artifact.sha256
            if (!verified.has(key)) {
                assert.equal(createHash('sha256').update(readFileSync(join(root,artifact.path))).digest('hex'),
                    artifact.sha256, 'benchmark artifact changed')
                verified.add(key)
            }
        }
        const recordedAt = Date.now()
        const target=await (await fetch(`${devtools}/json/new?about:blank`,{method:'PUT'})).json()
        ws=new WebSocket(target.webSocketDebuggerUrl)
        ws.addEventListener('message',event=>{
            const message=JSON.parse(event.data),entry=pending.get(message.id)
            if (entry) {
                clearTimeout(entry.timer);pending.delete(message.id)
                if (message.error) entry.reject(Error(JSON.stringify(message.error)))
                else entry.resolve(message.result)
            }
        })
        await new Promise((resolve,reject)=>{
            ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true})
        })
        const query=new URLSearchParams({program:base+'/'+item.program,args:JSON.stringify(item.args),
            host:item.host,threaded:String(item.threading==='multi')})
        await send('Page.navigate',{url:base+'/run?'+query})
        const deadline=Date.now()+30000
        let result
        while (Date.now()<deadline) {
            const reply=await send('Runtime.evaluate',{expression:'window.report',returnByValue:true})
            result=reply.result.value
            if (result) break
            await delay(10)
        }
        const row={...item,started:recordedAt/1000,browser:version.Browser,browser_flags:browserFlags,
            process_cpu_user_ms:null,process_cpu_system_ms:null,process_peak_rss_bytes:null,
            cold_scope:'fresh-page-or-worker; browser process retained; no-store HTTP',
            result:result??null,status:'failed'}
        if (result?.status===0 && result.error===null) {
            const values=result.stdout.filter(line=>line.startsWith('RESULT ')).map(line=>JSON.parse(line.slice(7)))
            if (item.mode==='baseline' || (values.length===1 &&
                values[0].operations===Number(item.args[1]) && values[0].workload===item.args[0] &&
                values[0].checksum===values[0].operations*(values[0].operations+1)/2)) {
                row.status='passed';row.measurement=values[0]??null
            }
        }
        if (row.status!=='passed') failures++
        appendFileSync(output,JSON.stringify(row)+'\n')
        console.log(`${row.status.toUpperCase()} ${item.id}`)
        ws.close();ws=null;await fetch(`${devtools}/json/close/${target.id}`)
        if (failures>=3) throw Error('Three failed observations; stop and diagnose before more sampling')
    }
} finally {
    for (const entry of pending.values()) clearTimeout(entry.timer)
    if (ws) ws.close()
    if (chrome && chrome.exitCode===null && chrome.signalCode===null) {
        const ended=once(chrome,'exit'),timer=setTimeout(()=>chrome.kill('SIGKILL'),5000)
        chrome.kill();try{await ended}finally{clearTimeout(timer)}
    }
    if (server) {server.closeAllConnections();server.close()}
    rmSync(profile,{recursive:true,force:true})
}
if (failures) process.exitCode=1
