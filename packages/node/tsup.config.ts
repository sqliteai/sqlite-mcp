import { defineConfig } from 'tsup';

export default defineConfig({
  entry: ['src/index.ts'],
  format: ['cjs', 'esm'],
  dts: true,
  splitting: false,
  sourcemap: true,
  clean: true,
  treeshake: true,
  minify: false,
  target: 'node16',
  // Defines `require` in the ESM bundle and `import.meta.url` in the CJS one
  shims: true,
  outDir: 'dist',
});
