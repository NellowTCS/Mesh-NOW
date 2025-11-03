// Lightweight devtools overlay - small replacement for eruda
// Captures console logs and shows a minimal overlay. Small, no deps.

type LogEntry = { level: 'log' | 'warn' | 'error'; msg: string; ts: number };

const logs: LogEntry[] = [];

function addLog(level: LogEntry['level'], args: any[]) {
    const text = args.map(a => {
        try { return typeof a === 'string' ? a : JSON.stringify(a); } catch { return String(a); }
    }).join(' ');
    logs.push({ level, msg: text, ts: Date.now() });
    // keep small
    if (logs.length > 200) logs.shift();
    renderLogs();
}

// Hijack console
const origLog = console.log.bind(console);
const origWarn = console.warn.bind(console);
const origError = console.error.bind(console);
console.log = (...args: any[]) => { origLog(...args); addLog('log', args); };
console.warn = (...args: any[]) => { origWarn(...args); addLog('warn', args); };
console.error = (...args: any[]) => { origError(...args); addLog('error', args); };

// Minimal DOM elements
let panel: HTMLDivElement | null = null;
let listEl: HTMLDivElement | null = null;

function createPanel() {
    if (panel) return;
    panel = document.createElement('div');
    panel.id = 'devtools-panel';
    panel.style.cssText = 'position:fixed;right:8px;bottom:8px;width:360px;height:220px;z-index:99999;font-family:monospace;font-size:12px;';

    const header = document.createElement('div');
    header.style.cssText = 'background:#222;color:#fff;padding:6px;border-radius:6px 6px 0 0;display:flex;justify-content:space-between;align-items:center;';
    header.textContent = 'DevTools';

    const controls = document.createElement('div');
    controls.style.cssText = 'display:flex;gap:6px;align-items:center;';

    const clearBtn = document.createElement('button');
    clearBtn.textContent = 'Clear';
    clearBtn.style.cssText = 'font-size:11px;padding:2px 6px;';
    clearBtn.onclick = () => { logs.length = 0; renderLogs(); };
    controls.appendChild(clearBtn);

    const closeBtn = document.createElement('button');
    closeBtn.textContent = '×';
    closeBtn.style.cssText = 'font-size:14px;padding:2px 6px;';
    closeBtn.onclick = () => togglePanel(false);
    controls.appendChild(closeBtn);

    header.appendChild(controls);
    panel.appendChild(header);

    listEl = document.createElement('div');
    listEl.style.cssText = 'background:#111;color:#eee;height:170px;overflow:auto;padding:6px;border-radius:0 0 6px 6px;';
    panel.appendChild(listEl);

    document.body.appendChild(panel);
}

function renderLogs() {
    if (!listEl) return;
    listEl.innerHTML = '';
    for (let i = logs.length - 1; i >= 0; i--) {
        const e = logs[i];
        const row = document.createElement('div');
        row.style.cssText = 'padding:2px 0;border-bottom:1px solid rgba(255,255,255,0.02);';
        const time = new Date(e.ts).toLocaleTimeString();
        row.innerHTML = `<span style="color:#888;margin-right:6px">${time}</span><span style="color:${e.level==='error'? '#ff6b6b': e.level==='warn'? '#ffd166':'#cbd5e1'}">${escapeHtml(e.msg)}</span>`;
        listEl.appendChild(row);
    }
}

function escapeHtml(s: string) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

let toggleButton: HTMLButtonElement | null = null;

export function initDevTools() {
    // create toggle button
    if (!toggleButton) {
        toggleButton = document.createElement('button');
        toggleButton.id = 'devtools-toggle';
        toggleButton.textContent = '\u2699';
        toggleButton.title = 'Toggle DevTools';
        toggleButton.style.cssText = 'position:fixed;right:12px;bottom:240px;z-index:99999;background:#111;color:#fff;border-radius:6px;padding:8px;border:1px solid rgba(255,255,255,0.06);cursor:pointer;font-size:16px;';
        toggleButton.onclick = () => {
            const visible = !!panel;
            if (visible) togglePanel(false); else togglePanel(true);
        };
        document.body.appendChild(toggleButton);
    }
    // keyboard toggle Ctrl+`
    window.addEventListener('keydown', (ev) => {
        if (ev.ctrlKey && ev.key === '`') {
            const visible = !!panel;
            togglePanel(!visible);
        }
    });
}

function togglePanel(show: boolean) {
    if (show) {
        createPanel();
        renderLogs();
    } else {
        if (panel) {
            panel.remove();
            panel = null;
            listEl = null;
        }
    }
}

// Simple fetch wrapper to log requests
const origFetch: typeof window.fetch = window.fetch.bind(window);
window.fetch = async (...args: Parameters<typeof window.fetch>) => {
    // @ts-ignore allow passing through
    const res = await origFetch(...(args as any));
    try {
        addLog('log', ['fetch', args[0], res.status]);
    } catch {}
    return res;
};

// auto-init only when running in browser and DOM ready
if (typeof document !== 'undefined') {
    if (document.readyState === 'complete' || document.readyState === 'interactive') {
        initDevTools();
    } else {
        window.addEventListener('DOMContentLoaded', () => initDevTools());
    }
}
