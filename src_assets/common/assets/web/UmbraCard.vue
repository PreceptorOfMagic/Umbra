<template>
  <!-- Umbra: replaces Apollo's version card on the home page (index.html keeps Apollo's, hidden).
       Strings: umbra.* in umbra_identity.js. -->
  <div class="card p-2 my-4 umbra-card">
    <div class="card-body">
      <h2 class="mb-1">{{ $t('umbra.title') }}</h2>
      <p class="text-secondary mb-3" v-if="installed">
        {{ installed.umbra !== null ? $t('umbra.version', { version: installed.raw }) : $t('umbra.dev_build', { version: installed.raw }) }}
        · {{ installed.base ? $t('umbra.built_on', { apollo: 'v' + installed.base }) : $t('umbra.base_unknown') }}
      </p>
      <p>{{ $t('umbra.thin_fork') }}</p>

      <!-- Update notice: Apollo's latest release against the release this Umbra is built on, and
           whether Umbra has a release built on it yet. Only stable releases are considered. -->
      <div v-if="state === 'loading'" class="alert alert-secondary">{{ $t('umbra.checking') }}</div>
      <template v-else>
        <div v-if="!apollo" class="alert alert-secondary">{{ $t('umbra.apollo_check_failed') }}</div>
        <div v-else-if="state === 'up_to_date'" class="alert alert-success">{{ $t('umbra.up_to_date', { apollo: apollo.tag }) }}</div>
        <div v-else-if="state === 'apollo_new_umbra_not'" class="alert alert-warning">
          <p class="mb-2">{{ $t('umbra.apollo_new_umbra_not', { apollo: apollo.tag }) }}</p>
          <a class="btn btn-secondary btn-sm" :href="apollo.url" target="_blank" rel="noopener">
            <i class="fas fa-fw fa-arrow-up-right-from-square"></i> {{ $t('umbra.apollo_notes') }}</a>
        </div>
        <div v-else-if="state === 'apollo_new_umbra_has'" class="alert alert-warning">
          <p class="mb-2">{{ $t('umbra.apollo_new_umbra_has', { apollo: apollo.tag, umbra: umbra.tag }) }}</p>
          <a class="btn btn-primary btn-sm m-1" :href="umbra.url" target="_blank" rel="noopener">{{ $t('umbra.download_umbra', { umbra: umbra.tag }) }}</a>
          <a class="btn btn-secondary btn-sm m-1" :href="apollo.url" target="_blank" rel="noopener">
            <i class="fas fa-fw fa-arrow-up-right-from-square"></i> {{ $t('umbra.apollo_notes') }}</a>
        </div>
        <div v-else-if="state === 'umbra_update'" class="alert alert-warning">
          <p class="mb-2">{{ $t('umbra.umbra_update', { umbra: umbra.tag }) }}</p>
          <a class="btn btn-primary btn-sm" :href="umbra.url" target="_blank" rel="noopener">{{ $t('umbra.download_umbra', { umbra: umbra.tag }) }}</a>
        </div>
        <p v-if="apollo && !umbra" class="text-secondary small mb-0">{{ $t('umbra.umbra_unknown') }}</p>
      </template>

      <h3 class="h5 mt-4">{{ $t('umbra.resources') }}</h3>
      <p class="mb-2">{{ $t('umbra.resources_desc') }}</p>
      <div class="mb-3">
        <a class="btn btn-primary m-1" :href="HUB" target="_blank" rel="noopener"><i class="fas fa-fw fa-house"></i> {{ $t('umbra.project_hub') }}</a>
        <a class="btn btn-secondary m-1" :href="REPO" target="_blank" rel="noopener"><i class="fab fa-fw fa-github"></i> {{ $t('umbra.source') }}</a>
        <a class="btn btn-secondary m-1" :href="REPO + '/issues'" target="_blank" rel="noopener"><i class="fas fa-fw fa-bug"></i> {{ $t('umbra.issues') }}</a>
        <a class="btn btn-secondary m-1" :href="ECLIPSE" target="_blank" rel="noopener"><i class="fas fa-fw fa-tv"></i> {{ $t('umbra.eclipse') }}</a>
      </div>

      <h3 class="h5 mt-4">{{ $t('umbra.legal') }}</h3>
      <p class="mb-1">{{ $t('umbra.modified', { apollo: installed && installed.base ? 'v' + installed.base : '?' }) }}</p>
      <p class="mb-1">{{ $t('umbra.licence') }}</p>
      <p class="mb-2">{{ $t('umbra.not_affiliated') }}</p>
      <div>
        <a class="btn btn-danger m-1" :href="REPO + '/blob/HEAD/LICENSE'" target="_blank" rel="noopener"><i class="fas fa-fw fa-file-alt"></i> {{ $t('umbra.licence_link') }}</a>
        <a class="btn btn-danger m-1" :href="REPO + '/blob/HEAD/LICENSES.md'" target="_blank" rel="noopener"><i class="fas fa-fw fa-scale-balanced"></i> {{ $t('umbra.notices_link') }}</a>
        <a class="btn btn-danger m-1" :href="REPO + '/blob/HEAD/THIRD-PARTY-NOTICES.md'" target="_blank" rel="noopener"><i class="fas fa-fw fa-exclamation"></i> {{ $t('umbra.third_party_link') }}</a>
        <a class="btn btn-danger m-1" :href="REPO" target="_blank" rel="noopener"><i class="fas fa-fw fa-code"></i> {{ $t('umbra.source_link') }}</a>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'

const REPO = 'https://github.com/PreceptorOfMagic/Umbra'
const HUB = 'https://preceptorofmagic.github.io/Eclipse-Umbra-Project-Hub/'
const ECLIPSE = 'https://github.com/PreceptorOfMagic/Eclipse'
const APOLLO_RELEASES = 'https://api.github.com/repos/ClassicOldSong/Apollo/releases'
const UMBRA_RELEASES = 'https://api.github.com/repos/PreceptorOfMagic/Umbra/releases'

const installed = ref(null)  // { raw, base: '0.4.6' | null, umbra: 1 | null }
const apollo = ref(null)     // { tag: 'v0.4.8', base: '0.4.8', url }
const umbra = ref(null)      // { tag: 'v0.4.8-umbra.1', base, n, url }
const state = ref('loading')

// Umbra versions are '<Apollo release>-umbra.<n>', e.g. 0.4.6-umbra.1 (BUILD_VERSION at build time).
// Anything else installed -- a dev build reads 0.0.0.<commit>[.dirty] -- has NO known Apollo base.
function parse(version) {
  const m = /^v?(\d+\.\d+\.\d+)(?:-umbra\.(\d+))?$/.exec(version || '')
  return m && m[1] !== '0.0.0' ? { base: m[1], umbra: m[2] !== undefined ? Number(m[2]) : null } : { base: null, umbra: null }
}

// A release tag: its leading x.y.z (Apollo pre-releases look like v0.4.7-alpha.1).
function parseTag(tag) {
  const m = /^v?(\d+\.\d+\.\d+)(?:-umbra\.(\d+))?/.exec(tag || '')
  return m ? { base: m[1], umbra: m[2] !== undefined ? Number(m[2]) : null } : { base: null, umbra: null }
}

function cmp(a, b) {
  const x = a.split('.').map(Number), y = b.split('.').map(Number)
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] - y[i]
  return 0
}

// Newest published release; pre-releases count only when the host's "PreRelease Notifications"
// setting (notify_pre_releases) is on, as for Apollo's own version card.
async function latest(releases, pre) {
  try {
    const r = await fetch(pre ? releases + '?per_page=10' : releases + '/latest')
    if (!r.ok) return null
    const body = await r.json()
    const j = pre ? (Array.isArray(body) ? body.find((x) => !x.draft) : null) : body
    return j && j.tag_name && (pre || !j.prerelease) ? { tag: j.tag_name, url: j.html_url, ...parseTag(j.tag_name) } : null
  } catch (e) {
    console.error(e)
    return null
  }
}

onMounted(async () => {
  let pre = false
  try {
    const config = await fetch('./api/config').then((r) => r.json())
    installed.value = { raw: config.version, ...parse(config.version) }
    // The web UI saves this checkbox as "enabled"/"disabled" (Checkbox.vue); accept what Apollo's own
    // to_bool (config.cpp) accepts.
    pre = ['true', 'yes', 'enable', 'enabled', 'on', '1'].includes(String(config.notify_pre_releases ?? '').trim().toLowerCase())
  } catch (e) {
    console.error(e)
  }
  const [a, u] = await Promise.all([latest(APOLLO_RELEASES, pre), latest(UMBRA_RELEASES, pre)])
  apollo.value = a && a.base ? a : null
  umbra.value = u && u.base ? u : null
  const base = installed.value && installed.value.base
  if (!apollo.value || !base) {
    state.value = 'unknown'
  } else if (cmp(apollo.value.base, base) > 0) {
    state.value = umbra.value && cmp(umbra.value.base, apollo.value.base) >= 0 ? 'apollo_new_umbra_has' : 'apollo_new_umbra_not'
  } else if (umbra.value && (cmp(umbra.value.base, base) > 0 ||
             (cmp(umbra.value.base, base) === 0 && (umbra.value.umbra ?? 0) > (installed.value.umbra ?? 0)))) {
    state.value = 'umbra_update'
  } else {
    state.value = 'up_to_date'
  }
})
</script>
