import './styles.css';
import { initDevTools } from './devtools';
import { serial as polyfillSerial } from 'web-serial-polyfill';

// Wire protocol payloads;
interface HelloInfo {
    mac: string;
    name: string;
    group_id: number;
    encrypted: boolean;
}

interface PeerInfo {
    mac: string;
    name?: string;
    online: boolean;
}

interface EventPeers {
    peers: PeerInfo[];
}

interface EventMessage {
    type: number;
    timestamp: number;
    sender: string;
    content: string;
    group_id: number;
    target: string;
}

interface SystemEvent {
    text: string;
}

// Native Web Serial on desktop; the polyfill implements the same interface on
// top of WebUSB for platforms (Android Chrome) where navigator.serial is
// missing.
const serial: Serial =
    'serial' in navigator
        ? navigator.serial
        : (polyfillSerial as unknown as Serial);

declare global {
    interface Window {
        meshNowApp?: MeshNowApp;
    }
}

const MSG_TYPE_CHAT = 1;
const MSG_TYPE_DIRECT = 2;
const MSG_TYPE_GROUP = 4;
const MSG_TYPE_PRESENCE = 5;
const MSG_TYPE_TYPING = 6;

// Deterministic color from MAC
function macColor(mac: string): string {
    let hash = 0;
    for (let i = 0; i < mac.length; i++) {
        hash = ((hash << 5) - hash + mac.charCodeAt(i)) | 0;
    }
    const hues = [210, 160, 30, 340, 270, 190, 50, 120];
    return `hsl(${hues[Math.abs(hash) % hues.length]}, 65%, 45%)`;
}

function shortMac(mac: string): string {
    const parts = mac.split(':');
    return parts.slice(-2).join(':').toUpperCase();
}

function formatTime(ts: number): string {
    if (!ts) return '';
    const d = new Date(ts);
    return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

function escapeHtml(s: string): string {
    const el = document.createElement('div');
    el.textContent = s;
    return el.innerHTML;
}

class MeshNowApp {
    private container!: HTMLElement;
    private messagesEl!: HTMLElement;
    private messageInput!: HTMLInputElement;
    private sendBtn!: HTMLButtonElement;
    private peerListEl!: HTMLElement;
    private peerCountEl!: HTMLElement;
    private statusEl!: HTMLElement;
    private typingEl!: HTMLElement;
    private nameEl!: HTMLElement;
    private targetSelect!: HTMLSelectElement;
    private sidebarEl!: HTMLElement;
    private connectBtn!: HTMLButtonElement;
    private groupId = 0;

    private selfMac = '';
    private selfName = '';
    private seenIds = new Set<string>();
    private typingPeers = new Map<string, number>();
    private typingTimer: ReturnType<typeof setTimeout> | null = null;
    private lastTypingSent = 0;
    private lastPeerSig = '';
    private targetMac: string | null = null; // null = broadcast
    private peerNames = new Map<string, string>();
    private port: SerialPort | null = null;
    private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
    private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
    private readBuffer = '';
    private connected = false;

    constructor() {
        this.buildUI();
        this.bindEvents();
        this.init();
        this.cleanTypingTimer();
    }

    private buildUI(): void {
        this.container = document.createElement('div');
        this.container.className = 'app';
        document.getElementById('app')!.appendChild(this.container);

        // Header
        const header = document.createElement('header');
        header.className = 'header';
        header.innerHTML = `
            <div class="header-top">
                <h1>Mesh-NOW</h1>
                <div class="header-meta">
                    <span class="status-dot"></span>
                    <span class="peer-count">0 peers</span>
                    <button class="connect-btn">Connect</button>
                </div>
            </div>
            <div class="header-name">
                <span class="name-label">Name:</span>
                <span class="name-value" title="Click to edit">unknown</span>
            </div>
        `;
        this.container.appendChild(header);

        this.statusEl = header.querySelector('.status-dot')!;
        this.peerCountEl = header.querySelector('.peer-count')!;
        this.nameEl = header.querySelector('.name-value')!;
        this.connectBtn = header.querySelector('.connect-btn')!;

        // Main content area
        const main = document.createElement('div');
        main.className = 'main';
        this.container.appendChild(main);

        // Sidebar
        const sidebar = document.createElement('aside');
        sidebar.className = 'sidebar';
        sidebar.innerHTML = `
            <div class="sidebar-header">Peers</div>
            <div class="peer-list"></div>
            <div class="sidebar-section">
                <div class="sidebar-label">Group</div>
                <div class="group-controls">
                    <input type="number" class="group-input" value="0" min="0" max="255" placeholder="0=off">
                    <button class="group-set-btn">Set</button>
                </div>
            </div>
            <div class="sidebar-section">
                <div class="sidebar-label">Encryption <span class="encryption-status"></span></div>
                <div class="group-controls">
                    <input type="text" class="encryption-input" placeholder="Key">
                    <button class="encryption-set-btn">Set</button>
                </div>
            </div>
        `;
        main.appendChild(sidebar);
        this.sidebarEl = sidebar;
        this.peerListEl = sidebar.querySelector('.peer-list')!;

        // Chat area
        const chatArea = document.createElement('div');
        chatArea.className = 'chat-area';
        main.appendChild(chatArea);

        // Chat header (target indicator)
        const chatHeader = document.createElement('div');
        chatHeader.className = 'chat-header';
        chatHeader.innerHTML = `<span class="chat-target">Broadcast</span>`;
        chatArea.appendChild(chatHeader);

        // Messages
        this.messagesEl = document.createElement('div');
        this.messagesEl.className = 'messages';
        chatArea.appendChild(this.messagesEl);

        // Typing indicator
        this.typingEl = document.createElement('div');
        this.typingEl.className = 'typing-indicator';
        this.typingEl.style.display = 'none';
        chatArea.appendChild(this.typingEl);

        // Input area
        const inputArea = document.createElement('div');
        inputArea.className = 'input-area';
        inputArea.innerHTML = `
            <select class="target-select">
                <option value="">Broadcast</option>
            </select>
            <input type="text" class="message-input" placeholder="Type a message..." maxlength="200">
            <button class="send-btn">Send</button>
        `;
        chatArea.appendChild(inputArea);

        this.messageInput = inputArea.querySelector('.message-input')!;
        this.sendBtn = inputArea.querySelector('.send-btn')!;
        this.targetSelect = inputArea.querySelector('.target-select')!;
    }

    private bindEvents(): void {
        this.connectBtn.addEventListener('click', () => this.toggleConnect());
        this.sendBtn.addEventListener('click', () => this.sendMessage());
        this.messageInput.addEventListener('keypress', (e) => {
            if (e.key === 'Enter') this.sendMessage();
        });

        // Typing indicator on input
        this.messageInput.addEventListener('input', () => this.onTyping());

        // Name editing
        this.nameEl.addEventListener('click', () => this.editName());

        // Target selection
        this.targetSelect.addEventListener('change', () => {
            const val = this.targetSelect.value;
            this.targetMac = val || null;
            const chatTarget = this.container.querySelector('.chat-target')!;
            if (val) {
                chatTarget.textContent = `DM: ${shortMac(val)}`;
                chatTarget.className = 'chat-target dm';
            } else {
                chatTarget.textContent =
                    this.groupId > 0 ? `Group ${this.groupId}` : 'Broadcast';
                chatTarget.className = 'chat-target';
            }
        });

        // Group set
        const groupBtn = this.sidebarEl.querySelector('.group-set-btn')!;
        const groupInput = this.sidebarEl.querySelector(
            '.group-input'
        ) as HTMLInputElement;
        groupBtn.addEventListener('click', () => {
            const id = parseInt(groupInput.value) || 0;
            this.groupId = id;
            console.log('set: group=' + id);
            this.sendFrame({ cmd: 'group', group_id: id });
            const chatTarget = this.container.querySelector('.chat-target')!;
            if (id > 0 && !this.targetMac) {
                chatTarget.textContent = `Group ${id}`;
                chatTarget.className = 'chat-target group';
            } else if (!this.targetMac) {
                chatTarget.textContent = 'Broadcast';
                chatTarget.className = 'chat-target';
            }
            this.addSystem(id > 0 ? `Joined group ${id}` : 'Left group');
        });

        // Encryption set
        const encBtn = this.sidebarEl.querySelector('.encryption-set-btn')!;
        const encInput = this.sidebarEl.querySelector(
            '.encryption-input'
        ) as HTMLInputElement;
        encBtn.addEventListener('click', () => {
            const key = encInput.value;
            if (!key) return;
            console.log('set: encryption key (' + key.length + ' chars)');
            this.sendFrame({ cmd: 'encryption', key });
            this.addSystem('Encryption key set');
            const encStatus = this.sidebarEl.querySelector(
                '.encryption-status'
            ) as HTMLElement;
            if (encStatus) encStatus.textContent = 'On';
            encInput.value = '';
        });
    }

    private init(): void {
        try {
            initDevTools();
        } catch (e) {
            /* ok */
        }
        this.setConnected(false);
    }

    private cleanTypingTimer(): void {
        setInterval(() => this.cleanTyping(), 1000);
    }

    // Connection

    private async toggleConnect(): Promise<void> {
        if (this.connected) {
            await this.disconnect();
            this.onDisconnect();
            return;
        }
        try {
            await this.connect();
            this.addSystem('Connecting...');
        } catch (e) {
            this.addSystem(`Connect failed: ${(e as Error).message}`, 'error');
        }
    }

    private async connect(): Promise<void> {
        if (this.port) {
            throw new Error('Already connected');
        }
        const port = await serial.requestPort();
        await port.open({ baudRate: 115200 });
        const readable = port.readable;
        if (!readable) {
            await port.close();
            throw new Error('Port opened without a readable stream');
        }
        this.port = port;
        this.reader = readable.getReader();
        this.connected = true;
        console.log(
            'serial: opened ' +
                port.getInfo().usbVendorId +
                ':' +
                port.getInfo().usbProductId
        );
        this.sendFrame({ cmd: 'hello' });
        this.readLoop().catch((e) => {
            this.addSystem(`Read error: ${(e as Error).message}`, 'error');
            this.onDisconnect();
        });
    }

    private async disconnect(): Promise<void> {
        try {
            await this.reader?.cancel();
        } catch {
            /* stream may already be closed */
        }
        try {
            await this.writer?.close();
        } catch {
            /* stream may already be closed */
        }
        try {
            await this.port?.close();
        } catch {
            /* port may already be closed */
        }
        this.reader = null;
        this.writer = null;
        this.port = null;
        this.readBuffer = '';
        this.connected = false;
        console.log('serial: closed');
    }

    private async readLoop(): Promise<void> {
        const decoder = new TextDecoder();
        while (this.reader && this.connected) {
            const res = await this.reader.read();
            if (res.done) {
                this.onDisconnect();
                return;
            }
            this.readBuffer += decoder.decode(res.value, { stream: true });
            let nl = this.readBuffer.indexOf('\n');
            while (nl >= 0) {
                const line = this.readBuffer.slice(0, nl).trim();
                this.readBuffer = this.readBuffer.slice(nl + 1);
                if (line) this.handleFrame(line);
                nl = this.readBuffer.indexOf('\n');
            }
        }
    }

    // Frames are newline-delimited JSON. The ESP-IDF console shares this
    // port and emits non-JSON log lines, so unparseable lines are dropped.
    private handleFrame(line: string): void {
        let frame: unknown;
        try {
            frame = JSON.parse(line);
        } catch {
            return;
        }
        const event = (frame as Record<string, unknown>).event;
        switch (event) {
            case 'hello':
                this.onHello(frame as HelloInfo);
                break;
            case 'message':
                this.onMessage(frame as EventMessage);
                break;
            case 'peers':
                this.onPeers(frame as EventPeers);
                break;
            case 'system':
                this.addSystem((frame as SystemEvent).text);
                break;
            default:
                break;
        }
    }

    private sendFrame(payload: Record<string, unknown>): void {
        if (!this.connected || !this.port) return;
        if (!this.writer) {
            const writable = this.port.writable;
            if (!writable) return;
            this.writer = writable.getWriter();
        }
        this.writer
            .write(new TextEncoder().encode(JSON.stringify(payload) + '\n'))
            .catch((e) => {
                this.addSystem(`Write error: ${(e as Error).message}`, 'error');
                this.onDisconnect();
            });
    }

    private setConnected(connected: boolean): void {
        this.statusEl.className =
            'status-dot ' + (connected ? 'online' : 'offline');
        this.connectBtn.textContent = connected ? 'Disconnect' : 'Connect';
        this.sendBtn.disabled = !connected;
    }

    private onDisconnect(): void {
        this.setConnected(false);
        this.selfMac = '';
        this.selfName = '';
        this.nameEl.textContent = 'unknown';
        this.nameEl.title = '';
        this.peerNames.clear();
        this.typingPeers.clear();
        this.renderPeerList([]);
        this.updateTargetSelect([]);
        this.peerCountEl.textContent = '0 peers';
        const encStatus = this.sidebarEl.querySelector(
            '.encryption-status'
        ) as HTMLElement;
        if (encStatus) encStatus.textContent = '';
        this.typingEl.style.display = 'none';
        this.addSystem('Disconnected');
    }

    // Transport events

    private onHello(h: HelloInfo): void {
        this.setConnected(true);
        this.selfMac = h.mac;
        this.selfName = h.name;
        this.nameEl.textContent = h.name;
        this.nameEl.title = h.mac;
        this.groupId = h.group_id;

        const groupInput = this.sidebarEl.querySelector(
            '.group-input'
        ) as HTMLInputElement;
        if (groupInput) groupInput.value = String(h.group_id);
        const chatTarget = this.container.querySelector('.chat-target')!;
        if (h.group_id > 0 && !this.targetMac) {
            chatTarget.textContent = `Group ${h.group_id}`;
            chatTarget.className = 'chat-target group';
        }
        const encStatus = this.sidebarEl.querySelector(
            '.encryption-status'
        ) as HTMLElement;
        if (encStatus) encStatus.textContent = h.encrypted ? 'On' : '';

        console.log(
            'hello: mac=' +
                h.mac +
                ' name=' +
                h.name +
                ' group=' +
                h.group_id +
                ' encrypted=' +
                h.encrypted
        );
        this.addSystem(`Connected to ${h.mac} as ${h.name}`);
    }

    private onMessage(msg: EventMessage): void {
        const dedupKey = `${msg.type}:${msg.timestamp}:${msg.sender}`;
        if (this.seenIds.has(dedupKey)) return;
        this.seenIds.add(dedupKey);
        if (this.seenIds.size > 500) {
            const first = this.seenIds.values().next().value!;
            this.seenIds.delete(first);
        }

        if (msg.type === MSG_TYPE_TYPING) {
            console.log(
                'recv: typing ' + shortMac(msg.sender) + '=' + msg.content
            );
            this.showTyping(msg.sender);
            return;
        }

        if (msg.type === MSG_TYPE_PRESENCE) {
            console.log(
                'recv: presence ' + shortMac(msg.sender) + '=' + msg.content
            );
            const senderName =
                this.peerNames.get(msg.sender) || shortMac(msg.sender);
            this.addSystem(`${senderName}: ${msg.content}`);
            return;
        }

        const isSelf = msg.sender === this.selfMac;
        const name = isSelf
            ? this.selfName
            : this.peerNames.get(msg.sender) || shortMac(msg.sender);
        console.log(
            'recv: type=' +
                msg.type +
                ' from=' +
                shortMac(msg.sender) +
                ' "' +
                msg.content +
                '"'
        );
        this.addMessage(
            msg.sender,
            name,
            msg.content,
            msg.type,
            msg.group_id,
            msg.target,
            msg.timestamp
        );
    }

    private onPeers(p: EventPeers): void {
        const peers = p.peers || [];
        this.peerCountEl.textContent = `${peers.length} peer${peers.length !== 1 ? 's' : ''}`;
        const sig = peers
            .map((x) => x.mac + (x.online ? '+' : ''))
            .sort()
            .join(',');
        if (sig !== this.lastPeerSig) {
            this.lastPeerSig = sig;
            console.log('peers: ' + (peers.length ? sig : '(none)'));
        }
        this.renderPeerList(peers);
        this.updateTargetSelect(peers);
    }

    // Sending

    private sendMessage(): void {
        const text = this.messageInput.value.trim();
        if (!text || !this.connected) return;

        this.messageInput.value = '';

        try {
            if (this.targetMac) {
                console.log(
                    'send: DM -> ' +
                        shortMac(this.targetMac) +
                        ' "' +
                        text +
                        '"'
                );
                this.sendFrame({
                    cmd: 'send',
                    target: this.targetMac,
                    message: text,
                });
                this.addMessage(
                    this.selfMac,
                    this.selfName,
                    text,
                    MSG_TYPE_DIRECT,
                    0,
                    this.targetMac,
                    Date.now()
                );
            } else if (this.groupId > 0) {
                console.log('send: group ' + this.groupId + ' "' + text + '"');
                this.sendFrame({
                    cmd: 'send',
                    group: this.groupId,
                    message: text,
                });
                this.addMessage(
                    this.selfMac,
                    this.selfName,
                    text,
                    MSG_TYPE_GROUP,
                    this.groupId,
                    '',
                    Date.now()
                );
            } else {
                console.log('send: broadcast "' + text + '"');
                this.sendFrame({ cmd: 'send', message: text });
                this.addMessage(
                    this.selfMac,
                    this.selfName,
                    text,
                    MSG_TYPE_CHAT,
                    0,
                    '',
                    Date.now()
                );
            }
        } catch (e) {
            this.addSystem('Failed to send', 'error');
        }
    }

    private onTyping(): void {
        if (this.typingTimer) clearTimeout(this.typingTimer);

        // Throttle
        const now = Date.now();
        if (
            this.targetMac &&
            this.connected &&
            now - this.lastTypingSent > 1500
        ) {
            this.lastTypingSent = now;
            this.sendFrame({
                cmd: 'typing',
                target: this.targetMac,
                typing: true,
            });
        }

        this.typingTimer = setTimeout(() => {
            if (this.targetMac && this.connected) {
                this.sendFrame({
                    cmd: 'typing',
                    target: this.targetMac,
                    typing: false,
                });
            }
            this.lastTypingSent = 0;
        }, 2000);
    }

    private editName(): void {
        const name = prompt('Set your node name:', this.selfName);
        if (!name || name === this.selfName) return;

        console.log('set: name="' + name + '"');
        this.sendFrame({ cmd: 'name', name });
        this.selfName = name;
        this.nameEl.textContent = name;
    }

    // Rendering

    private renderPeerList(peers: PeerInfo[]): void {
        this.peerListEl.innerHTML = '';
        this.peerNames.clear();

        if (peers.length === 0) {
            this.peerListEl.innerHTML =
                '<div class="peer-empty">No peers yet</div>';
            return;
        }

        for (const peer of peers) {
            const displayName = peer.name || shortMac(peer.mac);
            this.peerNames.set(peer.mac, displayName);

            const el = document.createElement('div');
            el.className = 'peer-item';
            const color = macColor(peer.mac);
            const isTyping = this.typingPeers.has(peer.mac);
            const statusClass = peer.online ? 'online' : 'offline';
            el.innerHTML = `
                <span class="peer-status-dot ${statusClass}"></span>
                <span class="peer-dot" style="background:${color}"></span>
                <span class="peer-name">${escapeHtml(displayName)}</span>
                ${isTyping ? '<span class="peer-typing">typing...</span>' : ''}
            `;
            el.addEventListener('click', () => {
                this.targetMac = peer.mac;
                this.targetSelect.value = peer.mac;
                const chatTarget =
                    this.container.querySelector('.chat-target')!;
                chatTarget.textContent = `DM: ${escapeHtml(displayName)}`;
                chatTarget.className = 'chat-target dm';
            });
            this.peerListEl.appendChild(el);
        }
    }

    private updateTargetSelect(peers: PeerInfo[]): void {
        const current = this.targetSelect.value;
        this.targetSelect.innerHTML = '<option value="">Broadcast</option>';
        for (const peer of peers) {
            const opt = document.createElement('option');
            opt.value = peer.mac;
            opt.textContent = peer.name || shortMac(peer.mac);
            if (peer.mac === current) opt.selected = true;
            this.targetSelect.appendChild(opt);
        }
    }

    private addMessage(
        senderMac: string,
        senderName: string,
        content: string,
        type: number,
        groupId: number,
        target: string,
        timestamp: number
    ): void {
        const isSelf = senderMac === this.selfMac;
        const isDM = type === MSG_TYPE_DIRECT;
        const isGroup = type === MSG_TYPE_GROUP;

        const el = document.createElement('div');
        el.className = `msg ${isSelf ? 'msg-sent' : 'msg-recv'} ${isDM ? 'msg-dm' : ''} ${isGroup ? 'msg-group' : ''}`;

        const color = isSelf ? 'rgba(255,255,255,0.95)' : macColor(senderMac);

        let badge = '';
        if (isDM) badge = '<span class="msg-badge">DM</span>';
        else if (isGroup)
            badge = `<span class="msg-badge msg-badge-group">Group ${groupId}</span>`;

        const time = formatTime(timestamp);

        el.innerHTML = `
            <div class="msg-header">
                <span class="msg-sender" style="color:${color}">${escapeHtml(senderName)}</span>
                ${badge}
                <span class="msg-time">${time}</span>
            </div>
            <div class="msg-body">${escapeHtml(content)}</div>
        `;

        this.messagesEl.appendChild(el);
        this.messagesEl.scrollTop = this.messagesEl.scrollHeight;
    }

    private addSystem(text: string, type: 'info' | 'error' = 'info'): void {
        const el = document.createElement('div');
        el.className = `msg-system ${type}`;
        el.textContent = text;
        this.messagesEl.appendChild(el);
        this.messagesEl.scrollTop = this.messagesEl.scrollHeight;
    }

    private showTyping(mac: string): void {
        this.typingPeers.set(mac, Date.now());
        const name =
            mac === this.selfMac
                ? this.selfName
                : this.peerNames.get(mac) || shortMac(mac);
        this.typingEl.textContent = `${name} is typing...`;
        this.typingEl.style.display = 'block';
    }

    private cleanTyping(): void {
        const now = Date.now();
        let changed = false;
        for (const [mac, ts] of this.typingPeers) {
            if (now - ts > 3000) {
                this.typingPeers.delete(mac);
                changed = true;
            }
        }
        if (changed) {
            if (this.typingPeers.size === 0) {
                this.typingEl.style.display = 'none';
            } else {
                const first = this.typingPeers.keys().next().value!;
                const name =
                    first === this.selfMac
                        ? this.selfName
                        : this.peerNames.get(first) || shortMac(first);
                this.typingEl.textContent = `${name} is typing...`;
            }
        }
    }
}

document.addEventListener('DOMContentLoaded', () => {
    if (window.meshNowApp) return;
    window.meshNowApp = new MeshNowApp();
});

export default MeshNowApp;
