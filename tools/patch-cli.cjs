const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '../ui/node_modules/aiot-vue-cli');
const patches = [
  ['src/libs/rollup.config.js', /commonjs\(\),(?:require\('@rollup\/plugin-typescript'\)\(\),)*/, "commonjs(),require('@rollup/plugin-typescript')(),"],
  ['src/libs/rollup.config.js', /const replaceValues = \{(?:\s*'defineComponent': ''\s*)?\}/, "const replaceValues = { 'defineComponent': '' }"],
  ['web-loaders/falcon-vue-loader/lib/parser.js', /compiler\.(?:parseComponent\(content, \{ pad: 'line' \}\)|parse\(content, \{ pad: 'line' \}\)\.descriptor)/, "compiler.parse(content, { pad: 'line' }).descriptor"],
  ['cli-libs/index.js', /path\.resolve\(__dirname, '\.\/vue\/packages\/vue-template-compiler\/index\.js'\)|'@vue\/compiler-sfc'/, "'@vue/compiler-sfc'"],
  ['src/libs/parser.js', /compiler\.(?:parseComponent\(content, \{ pad: true \}\)|parse\(content, \{ pad: true \}\)\.descriptor)/, 'compiler.parse(content, { pad: true }).descriptor'],
  ['web-loaders/falcon-vue-loader/lib/template-compiler/index.js', /compiler\.compile(?:Template)*/, 'compiler.compileTemplate'],
];

const files = new Map();
for (const [name, pattern, replacement] of patches) {
  const filename = path.join(root, name);
  const source = files.has(filename) ? files.get(filename) : fs.readFileSync(filename, 'utf8');
  if (!pattern.test(source)) throw new Error(`Unsupported aiot-vue-cli content: ${name}`);
  files.set(filename, source.replace(pattern, replacement));
}
for (const [filename, source] of files) {
  if (fs.readFileSync(filename, 'utf8') !== source) fs.writeFileSync(filename, source);
}
console.log('aiot-vue-cli Vue 3 / TypeScript compatibility patches ready');
