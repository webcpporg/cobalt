// Copyright (c) 2026 WebCpp.org
// Distributed under the Boost Software License, Version 1.0.

const query = new URLSearchParams(location.search)
const element = id => document.getElementById(id)
const program = query.get('program')
const variant = query.get('variant') ?? 'jspi'
const multi = query.get('threads') === 'multi'
const workerBuild = query.get('kind') === 'spsc'
const descriptions = {
    timer: ['Await a timer without blocking the browser. Cancellation stops the pending wait.',
        'asio::steady_timer timer(executor, delay);\nco_await timer.async_wait(cobalt::use_op);'],
    custom: ['A custom Cobalt operation completes immediately when its deadline has passed, or starts an asynchronous timer.',
        'void ready(handler h) override {\n    if (timer.expiry() < clock::now())\n        h(error_code{});\n}\nco_await wait_op(timer);'],
    channel: ['A producer sends ordered integers through a Cobalt channel. A consumer awaits each value. Cancellation ends the stream and joins the producer.',
        'auto producer = produce(channel, state);\nconst int value = co_await channel.read();\nco_await producer;'],
    outcome: ['Run both Outcome lazy and eager coroutines in a Cobalt main. Each adds one to ten; C++ checks both results even in a Release build.',
        'const int lazy = co_await lazy_value(10);\nconst int eager = co_await eager_value(10);\n// Both must equal 11.'],
    signals: ['Suspend a coroutine until a Signals2 callback delivers 42. The callback disconnects its single-shot connection before resuming the coroutine.',
        'auto pending = await_signal(signal);\nsignal(42);\nconst int value = co_await pending;'],
    spsc: ['A real Cobalt worker produces integers. The page-side coroutine receives them through a single-producer, single-consumer queue and verifies their order and checksum.',
        'auto worker = produce(queue, count, stop);\nconst int value = co_await queue.read();\nco_await worker;']
}
let nextRun = 0
let active = null
let frameCount = 0
const report = () => window.cobaltReport
window.cobaltReport = {status: 'ready', run: 0, completed: 0, frames: 0, values: []}
element('environment').textContent = `${variant === 'jspi' ? 'JSPI' : 'Asyncify'} build, ${multi ? 'pthread enabled' : 'single thread'}.`
for (const option of element('kind').options) option.disabled = (option.value === 'spsc') !== workerBuild
element('kind').value = workerBuild ? 'spsc' : 'timer'
if (workerBuild) {
    element('count').max = '1000000'
    element('count').value = '10000'
    element('delay').disabled = true
    element('bounds').textContent = 'Choose 1 to 1,000,000 values. The worker uses a bounded queue with 128 slots.'
}
const explain = () => {
    const [text, code] = descriptions[element('kind').value]
    element('explanation').textContent = text
    element('snippet').textContent = code
}
element('kind').addEventListener('change', explain)
explain()
const showError = message => {
    element('error').textContent = message
    element('error').hidden = false
}
const setStatus = (status, label) => {
    report().status = status
    element('status').textContent = label
}
const stopFrame = () => {
    if (!active) return
    clearTimeout(active.deadline)
    active.frame.remove()
    active = null
    element('start').disabled = false
    element('cancel').disabled = true
    element('kind').disabled = false
}
const fail = message => {
    report().error = message
    showError(message)
    setStatus('error', 'Stopped with an error')
    stopFrame()
}
const append = line => {
    const lines = element('trace').textContent.split('\n')
    lines.push(line)
    element('trace').textContent = lines.slice(-100).join('\n')
    element('trace').scrollTop = element('trace').scrollHeight
}
const start = event => {
    event.preventDefault()
    if (active) return
    element('error').hidden = true
    const count = Number(element('count').value)
    const delay = Number(element('delay').value)
    if (!Number.isInteger(count) || count < 1 || count > (workerBuild ? 1000000 : 10000) ||
        !Number.isInteger(delay) || delay < 0 || delay > 1000) {
        showError('Enter a whole-number count and wait within the ranges shown above.'); return
    }
    if (!workerBuild && count * delay > 60000) {
        showError('Keep each demonstration within 60 seconds by reducing the count or wait.'); return
    }
    if (!program || !program.endsWith('.js')) {
        showError('Select a compiled example using the program URL parameter.'); return
    }
    if (variant === 'jspi' && (typeof WebAssembly.Suspending !== 'function' || typeof WebAssembly.promising !== 'function')) {
        showError('This browser does not support JSPI. Open the Asyncify build.'); return
    }
    if (multi && (!crossOriginIsolated || typeof SharedArrayBuffer === 'undefined')) {
        showError('This pthread build needs a cross-origin isolated page with COOP and COEP headers.'); return
    }
    const run = ++nextRun
    window.cobaltReport = {status: 'loading', run, kind: element('kind').value,
        completed: 0, cancelled: false, exitStatus: null, frames: 0, values: []}
    frameCount = 0
    element('heartbeat').textContent = 'Waiting for the C++ program to start.'
    element('start').disabled = true
    element('kind').disabled = true
    element('cancel').disabled = false
    element('progress').max = count
    element('progress').value = 0
    element('trace').textContent = 'Loading the compiled C++ program...'
    element('result').textContent = 'Waiting for the C++ coroutine to start.'
    setStatus('loading', 'Loading')
    const frame = document.createElement('iframe')
    frame.title = 'Isolated Cobalt execution'
    const args = workerBuild ? [String(count)] : [report().kind, String(count), String(delay)]
    const params = new URLSearchParams({program, run: String(run), args: JSON.stringify(args)})
    frame.src = `bridge.html?${params}`
    active = {run, frame, count, deadline: setTimeout(() => fail('The program did not finish within 70 seconds.'), 70000)}
    document.body.appendChild(frame)
}
document.querySelector('form').addEventListener('submit', start)
element('cancel').addEventListener('click', () => {
    if (!active) return
    element('cancel').disabled = true
    element('status').textContent = 'Cancelling'
    active.frame.contentWindow.postMessage({owner: 'webcpp-cobalt', run: active.run, type: 'cancel'}, location.origin)
})
addEventListener('message', event => {
    const message = event.data
    if (!active || event.origin !== location.origin || event.source !== active.frame.contentWindow ||
        message?.owner !== 'webcpp-cobalt' || message.run !== active.run) return
    if (message.type === 'error') { fail(message.message); return }
    if (message.type === 'log') { append(message.line); return }
    if (message.type === 'started') setStatus('running', 'Running')
    if (message.type === 'progress') {
        report().completed = message.count
        if (report().values.length < 1000) report().values.push(message.value)
        element('progress').value = message.count
        element('result').textContent = `${message.count.toLocaleString()} of ${active.count.toLocaleString()} values completed.`
    }
    if (message.type === 'done') {
        report().completed = message.count
        report().cancelled = message.cancelled
        element('progress').value = message.count
    }
    if (message.type === 'finished') {
        report().exitStatus = message.status
        report().frames = frameCount
        element('heartbeat').textContent = `${frameCount} page frames while the program ran.`
        if (message.status !== 0) { fail(`The C++ program returned status ${message.status}. See its output above.`); return }
        setStatus(report().cancelled ? 'cancelled' : 'completed', report().cancelled ? 'Cancelled' : 'Completed')
        element('result').textContent = `${report().completed.toLocaleString()} values ${report().cancelled ? 'received before cancellation' : 'checked by C++'}.`
        stopFrame()
    }
})
function paint() {
    if (active && report().status === 'running') {
        ++frameCount
        element('heartbeat').textContent = `${frameCount} page frames while the program runs.`
        element('pulse').style.opacity = String(.4 + .6 * ((frameCount % 60) / 60))
    }
    requestAnimationFrame(paint)
}
requestAnimationFrame(paint)
addEventListener('pagehide', stopFrame)
