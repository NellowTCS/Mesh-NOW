import { defineConfig } from 'vite';

export default defineConfig({
    base: './',
    server: {
        port: 3000,
    },
    build: {
        rollupOptions: {
            input: {
                main: 'index.html',
                flasher: 'flasher.html',
            },
        },
    },
});
