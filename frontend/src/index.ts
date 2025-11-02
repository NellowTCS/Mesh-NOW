import './styles.css';

// Types
interface Message {
    sender: string;
    content: string;
    timestamp: number;
}

interface ApiResponse {
    messages: Message[];
}

// App class
class MeshNowApp {
    private messagesContainer!: HTMLElement;
    private messageInput!: HTMLInputElement;
    private sendButton!: HTMLButtonElement;
    private statusIndicator!: HTMLElement;
    private peerCount!: HTMLElement;

    constructor() {
        this.initializeElements();
        this.bindEvents();
        this.initializeApp();
        this.startPolling();
    }

    private initializeElements(): void {
        // Create main container
        const container = document.createElement('div');
        container.className = 'container';
        document.getElementById('app')!.appendChild(container);

        // Header
        const header = document.createElement('header');
        header.innerHTML = `
            <h1>Mesh-NOW</h1>
            <div class="status">
                <span id="status-indicator" class="status-connected">● Connected</span>
                <span id="peer-count">0 peers</span>
            </div>
        `;
        container.appendChild(header);

        // Messages container
        this.messagesContainer = document.createElement('div');
        this.messagesContainer.id = 'messages';
        this.messagesContainer.className = 'messages';
        container.appendChild(this.messagesContainer);

        // Input area
        const inputArea = document.createElement('div');
        inputArea.className = 'input-area';
        inputArea.innerHTML = `
            <input type="text" id="message-input" placeholder="Type your message..." maxlength="200">
            <button id="send-button">Send</button>
        `;
        container.appendChild(inputArea);

        // Get references
        this.messageInput = document.getElementById('message-input') as HTMLInputElement;
        this.sendButton = document.getElementById('send-button') as HTMLButtonElement;
        this.statusIndicator = document.getElementById('status-indicator') as HTMLElement;
        this.peerCount = document.getElementById('peer-count') as HTMLElement;
    }

    private bindEvents(): void {
        this.sendButton.addEventListener('click', () => this.sendMessage());
        this.messageInput.addEventListener('keypress', (e) => {
            if (e.key === 'Enter') {
                this.sendMessage();
            }
        });
    }

    private initializeApp(): void {
        this.addSystemMessage('Connected to ESP32 Mesh Network');
        this.updatePeerCount();
    }

    private async sendMessage(): Promise<void> {
        const message = this.messageInput.value.trim();
        if (!message) return;

        try {
            const response = await fetch('/send', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: `message=${encodeURIComponent(message)}`
            });

            if (response.ok) {
                this.messageInput.value = '';
                this.addMessage('You', message);
            } else {
                this.addSystemMessage('Failed to send message', 'error');
            }
        } catch (error) {
            console.error('Send error:', error);
            this.addSystemMessage('Network error', 'error');
        }
    }

    private async pollMessages(): Promise<void> {
        try {
            const response = await fetch('/messages');
            const data: ApiResponse = await response.json();

            if (data.messages && data.messages.length > 0) {
                data.messages.forEach(msg => {
                    this.addMessage(msg.sender, msg.content);
                });
            }
        } catch (error) {
            console.log('Poll error:', error);
        }
    }

    private startPolling(): void {
        setInterval(() => this.pollMessages(), 1000);
        setInterval(() => this.updatePeerCount(), 5000);
    }

    private addMessage(sender: string, content: string): void {
        const messageDiv = document.createElement('div');
        messageDiv.className = 'message';

        const senderDiv = document.createElement('div');
        senderDiv.className = 'message-sender';
        senderDiv.textContent = sender;

        const contentDiv = document.createElement('div');
        contentDiv.className = 'message-content';
        contentDiv.textContent = content;

        messageDiv.appendChild(senderDiv);
        messageDiv.appendChild(contentDiv);

        this.messagesContainer.appendChild(messageDiv);
        this.messagesContainer.scrollTop = this.messagesContainer.scrollHeight;
    }

    private addSystemMessage(content: string, type: 'info' | 'error' = 'info'): void {
        const messageDiv = document.createElement('div');
        messageDiv.className = `message system ${type}`;
        messageDiv.textContent = content;
        this.messagesContainer.appendChild(messageDiv);
        this.messagesContainer.scrollTop = this.messagesContainer.scrollHeight;
    }

    private async updatePeerCount(): Promise<void> {
        try {
            const response = await fetch('/peers');
            const data = await response.json();
            const count = data.peers || 0;
            this.peerCount.textContent = `${count} peer${count !== 1 ? 's' : ''}`;
        } catch (error) {
            // Silently fail for peer count updates
        }
    }
}

// Initialize app when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
    new MeshNowApp();
});

// Export for potential testing
export default MeshNowApp;