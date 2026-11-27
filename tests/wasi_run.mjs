// Runs a WASI (preview1) program under Node: node wasi_run.mjs prog.wasm [args...]
// The host root is preopened as "/", so pass absolute paths.
import { readFileSync } from 'node:fs';
import { WASI } from 'node:wasi';
import { argv, exit } from 'node:process';

const [, , wasmPath, ...args] = argv;
const wasi = new WASI({
  version: 'preview1',
  args: [wasmPath, ...args],
  env: {},
  preopens: { '/': '/' },
  returnOnExit: true,
});
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), wasi.getImportObject());
exit(wasi.start(instance));
