// Umbra: the host's name and its own wording, layered over Apollo's web UI at load time.
//
// Apollo's translation files are deliberately NOT edited. Every hand edit to them conflicts
// whenever Apollo updates its translations, and Umbra follows Apollo's releases (see
// docs/umbra-upstream-sync.md). Instead, locale.js passes the loaded messages through
// applyUmbraIdentity() before vue-i18n sees them:
//
//   1. In every language, the word "Apollo" -- which in Apollo's strings means the program you
//      are running -- becomes "Umbra". Case-sensitive on purpose: APOLLO_CLIENT_FPS and the other
//      environment-variable names stay exactly as they are.
//   2. A few strings describe Apollo ITSELF (its resource links, its recommended clients, its
//      legal documents). Those are replaced with Umbra's own wording, which makes clear the links
//      are Apollo's. Umbra has no translations of its own yet, so those keys are removed from the
//      other languages and fall back to the English below.
//   3. The `umbra.*` strings used by UmbraCard.vue are added.

const UMBRA_EN = {
  index: {
    description: 'Umbra is a self-hosted game stream host for Moonlight and Eclipse. It is Apollo with the additions Eclipse needs for co-op, and as little else changed as possible.',
  },
  client_card: {
    clients: "Apollo's recommended clients",
    clients_desc: 'Clients Apollo recommends. They work with Umbra too. These links go to other projects; Eclipse, the client made for Umbra, is in the Umbra panel above.',
  },
  resource_card: {
    resources: "Apollo's resources",
    resources_desc: "Kept from Apollo for convenience. These links go to Apollo's own GitHub pages: Umbra does not own or run them, and Apollo's maintainers do not support Umbra. Report Umbra problems in Umbra's issues (Umbra panel above).",
    legal: "Apollo's licence and notices",
    legal_desc: "Umbra is built from Apollo, so Apollo's licence and notices apply to the parts that come from it. These links go to Apollo's own GitHub pages; Umbra's legal information is in the Umbra panel above.",
  },
  umbra: {
    title: 'Umbra',
    version: 'Version {version}',
    dev_build: 'Development build {version}',
    built_on: 'Built on Apollo {apollo}.',
    base_unknown: 'This build does not record which Apollo release it is built on.',
    thin_fork: "Umbra is Apollo with the additions Eclipse needs for co-op, and as little else changed as possible. Apollo is ClassicOldSong's project and is still maintained; Umbra follows its releases instead of redoing its work.",
    checking: 'Checking for Apollo and Umbra releases...',
    up_to_date: "Up to date: built on Apollo's latest release ({apollo}).",
    apollo_new_umbra_not: 'Apollo {apollo} is out. Umbra has not been updated to it yet.',
    apollo_new_umbra_has: 'Apollo {apollo} is out, and Umbra {umbra} is built on it.',
    umbra_update: 'Umbra {umbra} is available.',
    apollo_check_failed: "Couldn't check Apollo's latest release.",
    umbra_unknown: "Couldn't find a published Umbra release to compare.",
    apollo_notes: "Apollo's release notes (Apollo's page)",
    download_umbra: 'Download Umbra {umbra}',
    resources: 'Umbra',
    resources_desc: 'Umbra and Eclipse are developed together.',
    project_hub: 'Project Hub',
    source: 'Umbra on GitHub',
    issues: 'Report an Umbra problem',
    eclipse: 'Eclipse (client)',
    legal: 'Umbra legal',
    modified: 'Umbra is a modified version of Apollo, which is itself based on Sunshine. Umbra\'s changes are by PreceptorOfMagic, 2026, starting from Apollo {apollo}.',
    licence: 'Umbra is free software under the GNU General Public License v3.0, the same licence as Apollo and Sunshine. It comes with NO WARRANTY, to the extent permitted by law.',
    not_affiliated: 'Umbra is an independent fork. It is not affiliated with or endorsed by ClassicOldSong (Apollo), SudoMaker or LizardByte (Sunshine); their names are used only to say where Umbra comes from.',
    licence_link: 'Licence (GPL-3.0)',
    notices_link: 'Umbra notices',
    third_party_link: 'Third-party notices',
    source_link: 'Source code',
  },
}

// Also the genitive "Apollos" (Swedish) -> "Umbras".
const APOLLO_WORD = /\bApollo(?=s?\b)/g

function rename(value) {
  if (typeof value === 'string') return value.replace(APOLLO_WORD, 'Umbra')
  if (Array.isArray(value)) return value.map(rename)
  if (value && typeof value === 'object') {
    const out = {}
    for (const [k, v] of Object.entries(value)) out[k] = rename(v)
    return out
  }
  return value
}

function merge(base, extra) {
  const out = { ...base }
  for (const [k, v] of Object.entries(extra)) {
    out[k] = v && typeof v === 'object' && !Array.isArray(v) ? merge(base?.[k] ?? {}, v) : v
  }
  return out
}

function strip(messages, extra) {
  const out = { ...messages }
  for (const [k, v] of Object.entries(extra)) {
    if (!(k in out)) continue
    if (v && typeof v === 'object' && !Array.isArray(v) && out[k] && typeof out[k] === 'object') out[k] = strip(out[k], v)
    else delete out[k]
  }
  return out
}

export function applyUmbraIdentity(messages) {
  for (const locale of Object.keys(messages)) {
    const renamed = rename(messages[locale])
    messages[locale] = locale === 'en' ? merge(renamed, UMBRA_EN) : strip(renamed, UMBRA_EN)
  }
  return messages
}
