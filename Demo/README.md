# Mesh-NOW Frontend

Modern TypeScript-based web interface for the ESP32 Mesh-NOW chat system.

## Architecture

- **TypeScript**: Type-safe development with modern JavaScript features
- **Modular CSS**: Clean, maintainable styles with CSS custom properties
- **Webpack**: Optimized bundling for embedded systems
- **Embedded**: Frontend files are embedded directly into ESP32 firmware

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

This starts a development server on `http://localhost:3000` with hot reloading.

### Production Build

```bash
npm run build
```

This creates optimized files in the `dist/` directory.

### Integration

The web UI connects to nodes over Web Serial (Chrome/Edge). Build the bundle with:

```bash
python scripts/build_frontend.py
```

or from inside `Demo/` with `npm run build`. Serve it locally with `npm run serve`
(or `npm run dev` for hot reload), open the page, and connect a node's USB port.
The Mesh-NOW node firmware exports a Web Serial API; it does not embed the
frontend.

## File Structure

```bash
Demo/
├── src/
│   ├── index.ts          # Main application entry point
│   └── styles.css        # Application styles
├── public/
│   └── index.html        # HTML template
├── dist/                 # Built files (generated)
├── package.json
├── tsconfig.json
└── webpack.config.js
```

## API Endpoints

The frontend communicates with these ESP32 endpoints:

- `GET /` - Main HTML page
- `GET /bundle.js` - JavaScript bundle
- `GET /styles.css` - CSS styles
- `POST /send` - Send a message
- `GET /messages` - Poll for new messages

## Features

- **Real-time messaging**: Automatic polling for new messages
- **Responsive design**: Works on desktop and mobile
- **TypeScript**: Full type safety and modern development experience
- **Clean UI**: Modern, accessible interface
- **Embedded optimized**: Minimal bundle size for ESP32 constraints

## Development Workflow

1. Make changes to TypeScript/CSS files
2. Test with `npm run dev`
3. Build with `npm run build`
4. Connect a node via Web Serial and chat
5. Build the node firmware with `cd Firmware && idf.py build`

## Bundle Size Optimization

The build is optimized for embedded systems:

- **Minified JavaScript**: Reduced file size
- **Extracted CSS**: Separate caching of styles
- **No external dependencies**: Self-contained bundle
- **ES2018 target**: Modern but widely supported

## Browser Support

- Chrome 70+
- Firefox 65+
- Safari 12+
- Edge 79+

Works on modern mobile browsers as well.
