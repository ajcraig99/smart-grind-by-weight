// Bundles sim/web into the single offline page sim/dist/index.html.
//   node build.mjs            (or: npm --prefix sim/web run build)
// Inlines: CSS, the esbuild bundle of src/main.js, and sim/out/wasm/grindtwin.js (wasm embedded, SINGLE_FILE).
import { build } from 'esbuild';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const simDir = path.resolve(here, '..');
const twinJs = path.join(simDir, 'out', 'wasm', 'grindtwin.js');
const outFile = path.join(simDir, 'dist', 'index.html');

function fail(msg) {
  console.error('build failed: ' + msg);
  process.exit(1);
}

if (!fs.existsSync(twinJs)) fail(`missing ${twinJs}; build it with sim/wasm/build.sh first`);

const bundled = await build({
  entryPoints: [path.join(here, 'src', 'main.js')],
  bundle: true,
  minify: true,
  format: 'iife',
  target: 'es2020',
  write: false,
  legalComments: 'none',
  logLevel: 'warning',
});
let bundle = bundled.outputFiles[0].text;
const css = fs.readFileSync(path.join(here, 'src', 'style.css'), 'utf8');
let template = fs.readFileSync(path.join(here, 'index.template.html'), 'utf8');

// Inline script text must not contain a closing script tag. The emscripten glue stores the wasm as a
// string that may hold raw NUL bytes; an HTML parser turns NUL into U+FFFD, which the glue's own decoder
// maps back to 0, so write U+FFFD directly and keep the file free of NUL bytes.
let twin = fs.readFileSync(twinJs, 'utf8').replace(/\u0000/g, '�');
const closer = /<\/script/i;
if (closer.test(twin)) fail('grindtwin.js contains a closing script tag');
if (twin.includes('<!--')) fail('grindtwin.js contains an HTML comment opener');
bundle = bundle.replace(/<\/script/gi, '<\\/script');
const cssSafe = css.replace(/<\/style/gi, '<\\/style');

for (const token of ['/*__CSS__*/', '/*__TWIN__*/', '/*__BUNDLE__*/']) {
  if (!template.includes(token)) fail('template lacks ' + token);
}
// Function replacers: the inlined text contains `$` sequences that String.replace would interpret.
template = template
  .replace('/*__CSS__*/', () => cssSafe)
  .replace('/*__TWIN__*/', () => twin)
  .replace('/*__BUNDLE__*/', () => bundle);

// ---- offline guard: nothing may load a resource over the network ----
const skeleton = fs.readFileSync(path.join(here, 'index.template.html'), 'utf8');
const ownText = { 'index.template.html': skeleton, 'style.css': css, 'bundle': bundle };
const urlRe = /https?:\/\/[^\s"'<>)]+/gi;
for (const [name, text] of Object.entries(ownText)) {
  const hits = text.match(urlRe);
  if (hits) fail(`${name} references URL(s): ${[...new Set(hits)].join(', ')}`);
}
if (/(<link\b|<script[^>]*\bsrc=|@import|\burl\(\s*['"]?(https?:)?\/\/)/i.test(skeleton + css)) {
  fail('template or css loads an external resource');
}
if (/\b(fetch|XMLHttpRequest|importScripts|sendBeacon|WebSocket|EventSource)\b|new Worker\(/.test(bundle)) {
  fail('bundle uses a network or worker API');
}
// URL-like strings inside the emscripten module are firmware string data; report them for the record.
const twinUrls = [...new Set(twin.match(urlRe) || [])];

fs.mkdirSync(path.dirname(outFile), { recursive: true });
fs.writeFileSync(outFile, template, 'utf8');
const bytes = fs.statSync(outFile).size;
console.log(`wrote ${path.relative(process.cwd(), outFile)}  ${(bytes / 1048576).toFixed(2)} MiB (${bytes} bytes)`);
console.log(`  bundle ${(bundle.length / 1024).toFixed(1)} KiB, css ${(css.length / 1024).toFixed(1)} KiB, twin ${(twin.length / 1048576).toFixed(2)} MiB chars`);
console.log(`  URL-like strings in the module (firmware data, never fetched): ${twinUrls.length ? JSON.stringify(twinUrls) : 'none'}`);
