# Mesh-NOW Frontend

TypeScript web interface for the ESP32 Mesh-NOW chat system, built with Vite as
a multi-page app.

## Architecture

- **TypeScript**: Type-safe development with modern JavaScript features
- **Modular CSS**: Clean, maintainable styles with CSS custom properties
- **Vite**: Fast dev server and optimized production bundling
- **Multi-page**: `index.html` (chat UI) and `flasher.html` (web installer) are
  separate entry points sharing one build
- **Web flasher**: The installer page flashes node firmware over Web Serial
  with `esptool-js`, driven by firmware manifests published to GitHub Pages

## Development

### Prerequisites

- Node.js 20+
- npm

### Setup

```bash
cd Demo
npm install
```

### Development Server

```bash
npm run dev
```

This starts the Vite dev server on `http://localhost:3000`. The chat UI is at
`/` and the web installer at `/flasher.html`.

### Production Build

```bash
npm run build
```

This creates optimized files in the `dist/` directory with `./`-relative asset
paths, so both pages work when served from a project-site subpath.

### Integration

The chat UI connects to nodes over Web Serial (Chrome/Edge). Build the bundle
with:

```bash
python scripts/build_frontend.py
```

or from inside `Demo/` with `npm run build`. Serve it locally with `npm run
serve` (or `npm run dev` for hot reload), open the page, and connect a node's
USB port. The Mesh-NOW node firmware exports a Web Serial API; it does not
embed the frontend.

## File Structure

```bash
Demo/
├── src/
│   ├── index.ts          # Chat application entry point
│   ├── styles.css        # Chat application styles
│   └── flasher/
│       ├── main.ts       # Web installer entry point
│       ├── style.css     # Web installer styles
│       └── types.ts      # Flasher manifest types
├── index.html            # Chat page template
├── flasher.html          # Web installer page template
├── dist/                 # Built files (generated)
├── vite.config.ts        # Vite multi-page config
├── package.json
└── tsconfig.json
```

## Protocol

The frontend speaks a JSON line protocol over the node's USB serial connection
(UART0 or the USB-serial/JTAG port, depending on the target). There is no HTTP
interface on the device; the browser owns the serial port while connected.

## Features

- **Real-time messaging**: Automatic refresh of peer presence and messages
- **Group and DM routing**: Target messages per peer or per group
- **Responsive design**: Works on desktop and mobile
- **TypeScript**: Full type safety and modern development experience
- **Clean UI**: Modern, accessible interface
- **Web installer**: Version-targeted firmware flashing with data-preserving
  updates

## Development Workflow

1. Make changes to TypeScript/CSS files
2. Test with `npm run dev`
3. Build with `npm run build`
4. Connect a node via Web Serial and chat
5. Build the node firmware with `cd Firmware && idf.py build`

## Browser Support

- Chrome 89+ (Web Serial and the web installer require Chromium)

The web installer requires a Chromium-based browser with the Web Serial API.
