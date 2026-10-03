#!/usr/bin/env node
// Umbra artwork: every icon Umbra ships, rendered from ONE master -- brand-mark.svg, a byte-identical
// copy of the Eclipse/Umbra Project Hub's site/assets/brand-mark.svg (the GitHub banner mark: purple
// outlined rounded square, two flat violet panels, white seam, no centre dot).
//
//   npm ci --prefix scripts/umbra-artwork && node scripts/umbra-artwork/render.cjs
//
// Outputs keep APOLLO'S FILENAMES (apollo.ico, logo-apollo-45.png, apollo-playing.ico, ...): the
// executable, installer, tray and web UI load them by those names, so swapping the pictures under
// the same names keeps Umbra's change out of Apollo's code (docs/umbra-upstream-sync.md).
// The tray's playing / pausing / locked states are the mark with a status badge, as Apollo's were.
// manifest.json records the renderer and the sha256 of the master and of every output.

const fs = require('node:fs')
const path = require('node:path')
const crypto = require('node:crypto')

let sharp
try { sharp = require('sharp') } catch {
  try { sharp = require(process.env.UMBRA_SHARP) } catch {
    console.error('install the renderer first: npm ci --prefix scripts/umbra-artwork (or set UMBRA_SHARP to a sharp 0.35.4 module path)')
    process.exit(1)
  }
}

const here = __dirname
const root = path.resolve(here, '..', '..')
const images = 'src_assets/common/assets/web/public/images'
const master = fs.readFileSync(path.join(here, 'brand-mark.svg'), 'utf8')
const inner = /<g>([\s\S]*?)<\/g>/.exec(master)[1]
const viewBox = /viewBox="([^"]+)"/.exec(master)[1]

// Status badges, bottom-right, on the same 68-unit canvas as the mark (viewBox -2 -2 68 68).
const ring = '<circle cx="52" cy="52" r="11.3" fill="#09090d" stroke="#fff" stroke-width="2.1"/>'
const BADGES = {
  playing: { desc: 'with a green play symbol', svg: ring + '<path d="M48.5 45.6 58.4 52 48.5 58.4Z" fill="#2eca33"/>' },
  pausing: { desc: 'with a blue pause symbol', svg: ring + '<rect x="47" y="45.6" width="3.6" height="12.8" rx="1.4" fill="#29a8ff"/><rect x="53.4" y="45.6" width="3.6" height="12.8" rx="1.4" fill="#29a8ff"/>' },
  locked: { desc: 'with an amber padlock', svg: ring + '<path d="M47.75 50.6v-2.1a4.25 4.25 0 0 1 8.5 0v2.1" fill="none" stroke="#f59e0b" stroke-width="2.8" stroke-linecap="round"/><rect x="45.6" y="49.9" width="12.75" height="9.2" rx="2.1" fill="#f59e0b"/><circle cx="52" cy="54.1" r="1.4" fill="#09090d"/>' },
}
function stateSvg(state) {
  const b = BADGES[state]
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}" role="img" aria-labelledby="title desc">
  <title id="title">Umbra ${state}</title>
  <desc id="desc">The Eclipse/Umbra split-screen mark ${b.desc}.</desc>
  <g>${inner}</g>
  ${b.svg}
</svg>
`
}

const png = (svg, size) => sharp(Buffer.from(svg), { density: Math.max(72, Math.ceil(72 * size / 68) * 2) })
  .resize(size, size, { fit: 'fill' }).png({ compressionLevel: 9, palette: false }).toBuffer()

function ico(pngs) {  // [{size, data}] -> .ico with PNG-compressed entries
  const head = Buffer.alloc(6 + 16 * pngs.length)
  head.writeUInt16LE(0, 0); head.writeUInt16LE(1, 2); head.writeUInt16LE(pngs.length, 4)
  let offset = head.length
  pngs.forEach(({ size, data }, i) => {
    const e = 6 + 16 * i
    head.writeUInt8(size >= 256 ? 0 : size, e); head.writeUInt8(size >= 256 ? 0 : size, e + 1)
    head.writeUInt16LE(1, e + 4); head.writeUInt16LE(32, e + 6)
    head.writeUInt32LE(data.length, e + 8); head.writeUInt32LE(offset, e + 12)
    offset += data.length
  })
  return Buffer.concat([head, ...pngs.map((p) => p.data)])
}

function icns(entries) {  // [{type, data}] -> .icns (PNG payloads)
  const parts = entries.map(({ type, data }) => {
    const h = Buffer.alloc(8); h.write(type, 0, 'ascii'); h.writeUInt32BE(data.length + 8, 4)
    return Buffer.concat([h, data])
  })
  const body = Buffer.concat(parts)
  const h = Buffer.alloc(8); h.write('icns', 0, 'ascii'); h.writeUInt32BE(body.length + 8, 4)
  return Buffer.concat([h, body])
}

const ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex')
const written = []
function write(rel, data) {
  fs.writeFileSync(path.join(root, rel), data)
  written.push({ path: rel, sha256: sha(Buffer.isBuffer(data) ? data : Buffer.from(data)) })
}

async function main() {
  const variants = { mark: master, ...Object.fromEntries(Object.keys(BADGES).map((s) => [s, stateSvg(s)])) }
  const rendered = {}
  for (const [name, svg] of Object.entries(variants)) {
    rendered[name] = {}
    for (const size of [...ICO_SIZES, 45, 512, 1024]) rendered[name][size] = await png(svg, size)
  }
  const icoOf = (name) => ico(ICO_SIZES.map((size) => ({ size, data: rendered[name][size] })))
  const m = rendered.mark

  // Executable, installer and macOS bundle icons.
  write('apollo.svg', master)
  write('apollo.png', m[1024])
  write('apollo.ico', icoOf('mark'))
  write('apollo.icns', icns([
    { type: 'ic11', data: m[32] }, { type: 'ic12', data: m[64] }, { type: 'ic07', data: m[128] },
    { type: 'ic13', data: m[256] }, { type: 'ic08', data: m[256] }, { type: 'ic14', data: m[512] },
    { type: 'ic09', data: m[512] }, { type: 'ic10', data: m[1024] },
  ]))
  // Web UI: navbar logo, favicon; tray: idle icon.
  write(`${images}/logo-apollo.svg`, master)
  write(`${images}/logo-apollo-16.png`, m[16])
  write(`${images}/logo-apollo-45.png`, m[45])
  write(`${images}/apollo.ico`, icoOf('mark'))
  // Tray states.
  for (const state of Object.keys(BADGES)) {
    write(`${images}/apollo-${state}.svg`, variants[state])
    write(`${images}/apollo-${state}.png`, rendered[state][1024])
    write(`${images}/apollo-${state}-16.png`, rendered[state][16])
    write(`${images}/apollo-${state}-45.png`, rendered[state][45])
    write(`${images}/apollo-${state}.ico`, icoOf(state))
  }

  const manifest = {
    renderer: `sharp ${sharp.versions?.sharp ?? require('sharp/package.json').version}`,
    master: { path: 'scripts/umbra-artwork/brand-mark.svg', sha256: sha(Buffer.from(master)), from: 'Eclipse-Umbra-Project-Hub site/assets/brand-mark.svg' },
    outputs: written,
  }
  fs.writeFileSync(path.join(here, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n')
  console.log(`rendered ${written.length} files from ${manifest.master.path} (${manifest.renderer})`)
}

main().catch((e) => { console.error(e); process.exit(1) })
