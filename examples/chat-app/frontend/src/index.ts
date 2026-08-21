import './styles.css';
import { initDevTools } from './devtools';

interface Message {
    sender: string;
    content: string;
    timestamp: number;
    type: number;
    group_id: number;
    target: string;
}

interface SelfInfo {
    mac: string;
    name: string;
}

interface PeersResponse {
    peers: string[];
}

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
    private groupId = 0;

    private selfMac = '';
    private selfName = '';
    private seenIds = new Set<number>();
    private typingPeers = new Map<string, number>();
    private typingTimer: ReturnType<typeof setTimeout> | null = null;
    private targetMac: string | null = null; // null = broadcast

    constructor() {
        this.buildUI();
        this.bindEvents();
        this.init();
        this.startPolling();
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
                </div>
            </div>
            <div class="header-name">
                <span class="name-label">Name:</span>
                <span class="name-value" title="Click to edit"></span>
            </div>
        `;
        this.container.appendChild(header);

        this.statusEl = header.querySelector('.status-dot')!;
        this.peerCountEl = header.querySelector('.peer-count')!;
        this.nameEl = header.querySelector('.name-value')!;

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
                <div class="sidebar-label">Encryption</div>
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
                chatTarget.textContent = this.groupId > 0 ? `Group ${this.groupId}` : 'Broadcast';
                chatTarget.className = 'chat-target';
            }
        });

        // Group set
        const groupBtn = this.sidebarEl.querySelector('.group-set-btn')!;
        const groupInput = this.sidebarEl.querySelector('.group-input') as HTMLInputElement;
        groupBtn.addEventListener('click', () => {
            const id = parseInt(groupInput.value) || 0;
            this.groupId = id;
            fetch('/group', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `group_id=${id}`
            }).then(() => {
                const chatTarget = this.container.querySelector('.chat-target')!;
                if (id > 0 && !this.targetMac) {
                    chatTarget.textContent = `Group ${id}`;
                    chatTarget.className = 'chat-target group';
                } else if (!this.targetMac) {
                    chatTarget.textContent = 'Broadcast';
                    chatTarget.className = 'chat-target';
                }
                this.addSystem(id > 0 ? `Joined group ${id}` : 'Left group');
            }).catch(() => {});
        });

        // Encryption set
        const encBtn = this.sidebarEl.querySelector('.encryption-set-btn')!;
        const encInput = this.sidebarEl.querySelector('.encryption-input') as HTMLInputElement;
        encBtn.addEventListener('click', () => {
            const key = encInput.value;
            if (!key) return;
            fetch('/encryption', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `key=${encodeURIComponent(key)}`
            }).then(() => {
                this.addSystem('Encryption key set');
                encInput.value = '';
            }).catch(() => {});
        });
    }

    private async init(): Promise<void> {
        try { initDevTools(); } catch (e) { /* ok */ }

        // Load self info
        try {
            const resp = await fetch('/self');
            const data: SelfInfo = await resp.json();
            this.selfMac = data.mac;
            this.selfName = data.name;
            this.nameEl.textContent = this.selfName;
            this.nameEl.title = this.selfMac;
        } catch (e) {
            console.warn('Failed to load self info');
        }

        // Load WiFi info
        try {
            const resp = await fetch('/wifi-info');
            const data = await resp.json();
            this.addSystem(`Connected to ${data.ssid}`);
        } catch (e) {
            this.addSystem('Connected to mesh network');
        }

        this.updatePeers();
    }

    private startPolling(): void {
        setInterval(() => this.pollMessages(), 1000);
        setInterval(() => this.updatePeers(), 3000);
        setInterval(() => this.cleanTyping(), 1000);
    }

    // Sending

    private async sendMessage(): Promise<void> {
        const text = this.messageInput.value.trim();
        if (!text) return;

        this.messageInput.value = '';

        try {
            if (this.targetMac) {
                const body = `target=${encodeURIComponent(this.targetMac)}&message=${encodeURIComponent(text)}`;
                await fetch('/send/direct', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body
                });
                this.addMessage(this.selfMac, this.selfName, text, MSG_TYPE_DIRECT, 0, this.targetMac);
            } else if (this.groupId > 0) {
                const body = `group_id=${this.groupId}&message=${encodeURIComponent(text)}`;
                await fetch('/send/group', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body
                });
                this.addMessage(this.selfMac, this.selfName, text, MSG_TYPE_GROUP, this.groupId, '');
            } else {
                await fetch('/send', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: `message=${encodeURIComponent(text)}`
                });
                this.addMessage(this.selfMac, this.selfName, text, MSG_TYPE_CHAT, 0, '');
            }
        } catch (e) {
            this.addSystem('Failed to send', 'error');
        }
    }

    private async onTyping(): Promise<void> {
        if (this.typingTimer) clearTimeout(this.typingTimer);

        if (this.targetMac) {
            try {
                const body = `target=${encodeURIComponent(this.targetMac)}&typing=true`;
                await fetch('/typing', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
            } catch (e) { /* ok */ }
        }

        this.typingTimer = setTimeout(() => {
            if (this.targetMac) {
                const body = `target=${encodeURIComponent(this.targetMac)}&typing=false`;
                fetch('/typing', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body }).catch(() => { });
            }
        }, 2000);
    }

    private async editName(): Promise<void> {
        const name = prompt('Set your node name:', this.selfName);
        if (!name || name === this.selfName) return;

        try {
            await fetch('/name', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `name=${encodeURIComponent(name)}`
            });
            this.selfName = name;
            this.nameEl.textContent = name;
        } catch (e) {
            this.addSystem('Failed to set name', 'error');
        }
    }

    // Polling

    private async pollMessages(): Promise<void> {
        try {
            const resp = await fetch('/messages');
            const data = await resp.json();
            if (!data.messages) return;

            for (const msg of data.messages) {
                if (this.seenIds.has(msg.timestamp + msg.type)) continue;
                this.seenIds.add(msg.timestamp + msg.type);

                if (msg.type === MSG_TYPE_TYPING) {
                    this.showTyping(msg.sender);
                    continue;
                }

                if (msg.type === MSG_TYPE_PRESENCE) {
                    this.addSystem(`${shortMac(msg.sender)}: ${msg.content}`);
                    continue;
                }

                const isSelf = msg.sender === this.selfMac;
                const name = isSelf ? this.selfName : shortMac(msg.sender);
                this.addMessage(msg.sender, name, msg.content, msg.type, msg.group_id, msg.target);

                // Cap seen set
                if (this.seenIds.size > 500) {
                    const first = this.seenIds.values().next().value!;
                    this.seenIds.delete(first);
                }
            }
        } catch (e) { /* ok */ }
    }

    private async updatePeers(): Promise<void> {
        try {
            const resp = await fetch('/peers');
            const data: PeersResponse = await resp.json();
            const peers = data.peers || [];
            this.peerCountEl.textContent = `${peers.length} peer${peers.length !== 1 ? 's' : ''}`;
            this.renderPeerList(peers);
            this.updateTargetSelect(peers);
        } catch (e) { /* ok */ }
    }

    // Rendering

    private renderPeerList(peers: string[]): void {
        this.peerListEl.innerHTML = '';
        if (peers.length === 0) {
            this.peerListEl.innerHTML = '<div class="peer-empty">No peers yet</div>';
            return;
        }

        for (const mac of peers) {
            const el = document.createElement('div');
            el.className = 'peer-item';
            const color = macColor(mac);
            const isTyping = this.typingPeers.has(mac);
            el.innerHTML = `
                <span class="peer-dot" style="background:${color}"></span>
                <span class="peer-name">${shortMac(mac)}</span>
                ${isTyping ? '<span class="peer-typing">typing...</span>' : ''}
            `;
            el.addEventListener('click', () => {
                this.targetMac = mac;
                this.targetSelect.value = mac;
                const chatTarget = this.container.querySelector('.chat-target')!;
                chatTarget.textContent = `DM: ${shortMac(mac)}`;
                chatTarget.className = 'chat-target dm';
            });
            this.peerListEl.appendChild(el);
        }
    }

    private updateTargetSelect(peers: string[]): void {
        const current = this.targetSelect.value;
        this.targetSelect.innerHTML = '<option value="">Broadcast</option>';
        for (const mac of peers) {
            const opt = document.createElement('option');
            opt.value = mac;
            opt.textContent = shortMac(mac);
            if (mac === current) opt.selected = true;
            this.targetSelect.appendChild(opt);
        }
    }

    private addMessage(senderMac: string, senderName: string, content: string, type: number, groupId: number, target: string): void {
        const isSelf = senderMac === this.selfMac;
        const isDM = type === MSG_TYPE_DIRECT;
        const isGroup = type === MSG_TYPE_GROUP;

        const el = document.createElement('div');
        el.className = `msg ${isSelf ? 'msg-sent' : 'msg-recv'} ${isDM ? 'msg-dm' : ''} ${isGroup ? 'msg-group' : ''}`;

        const color = isSelf ? 'var(--accent)' : macColor(senderMac);

        let badge = '';
        if (isDM) badge = '<span class="msg-badge">DM</span>';
        else if (isGroup) badge = `<span class="msg-badge msg-badge-group">Group ${groupId}</span>`;

        el.innerHTML = `
            <div class="msg-header">
                <span class="msg-sender" style="color:${color}">${escapeHtml(senderName)}</span>
                ${badge}
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
        const name = mac === this.selfMac ? this.selfName : shortMac(mac);
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
                const name = first === this.selfMac ? this.selfName : shortMac(first);
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
