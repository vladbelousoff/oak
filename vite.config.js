import { defineConfig } from 'vite';
import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';

const projectRoot = path.dirname(fileURLToPath(import.meta.url));

// In dev, proxy /build_wasm/* straight from the cmake output directory,
// since Vite root is www/ and can't reach ../build_wasm/ on its own.
function wasmDevPlugin() {
  return {
    name: 'wasm-dev',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use('/build_wasm', (req, res, next) => {
        const file = path.join(projectRoot, 'build_wasm', req.url ?? '');
        const safe = path.join(projectRoot, 'build_wasm');
        if (!file.startsWith(safe)) { next(); return; }
        if (!fs.existsSync(file) || !fs.statSync(file).isFile()) { next(); return; }
        const ct = file.endsWith('.wasm') ? 'application/wasm' : 'application/javascript';
        res.setHeader('Content-Type', ct);
        fs.createReadStream(file).pipe(res);
      });
    },
  };
}

export default defineConfig({
  root: 'www',
  plugins: [wasmDevPlugin()],
  server: { open: '/' },
  build: {
    outDir: '../_site',
    emptyOutDir: true,
  },
});
