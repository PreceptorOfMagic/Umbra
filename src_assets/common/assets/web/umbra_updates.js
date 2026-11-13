// Plain-JS copy of Umbra (Vibepollo)'s umbra/updates.ts: keep the two identical in behaviour.
// Umbra: what the Project Hub's updates.json says about a newer Umbra (the same file and rules as Eclipse's
// update notice; reference: docs/update-notice.md in the Eclipse repository).
//
// A release with "notify": false is SILENT: it never shows a notice on its own, and is listed beneath a
// later announced release. "platforms" limits a release to those platforms. Only schema 1 is read; any
// other document shows nothing.

function parseVersion(v) {
  const m = /^(?:umbra-(?:linux-)?)?v?(\d+(?:\.\d+)*)(?:-(.+))?$/.exec((v || '').trim());
  if (!m) return null;
  const nums = m[1].split('.').map(Number);
  const suffix = m[2] ?? null;
  const u = suffix ? /^umbra\.(\d+)$/.exec(suffix) : null;
  return { nums, umbra: u ? Number(u[1]) : 0, pre: suffix && !u ? suffix : null };
}

export function compareVersions(a, b) {
  const x = parseVersion(a);
  const y = parseVersion(b);
  if (!x || !y) return 0;
  for (let i = 0; i < Math.max(x.nums.length, y.nums.length); i++) {
    const d = (x.nums[i] ?? 0) - (y.nums[i] ?? 0);
    if (d !== 0) return d;
  }
  if (x.umbra !== y.umbra) return x.umbra - y.umbra;
  if (x.pre !== y.pre) return x.pre === null ? 1 : y.pre === null ? -1 : x.pre < y.pre ? -1 : 1;
  return 0;
}

function applies(release, platform) {
  const p = release.platforms;
  return !Array.isArray(p) || p.includes(platform);
}

/** null: nothing to show (no announced newer release, or a document this code cannot read). */
export function evaluateUpdates(doc, product, platform, installed) {
  if (!doc || typeof doc !== 'object' || doc.schema !== 1) return null;
  const prod = doc[product];
  const releases = prod?.releases;
  if (!Array.isArray(releases) || !parseVersion(installed)) return null;
  const usable = releases.filter(
    (r) =>
      !!r && typeof r === 'object' && typeof r.version === 'string' && !!parseVersion(r.version) && applies(r, platform),
  );
  const newer = usable.filter((r) => compareVersions(r.version, installed) > 0);
  let latest = null;
  for (const r of newer) {
    if (r.notify !== false && (latest === null || compareVersions(r.version, latest) > 0)) latest = r.version;
  }
  if (latest === null) return null;
  const listed = newer
    .filter((r) => compareVersions(r.version, latest) <= 0)
    .sort((a, b) => compareVersions(b.version, a.version))
    .map((r) => ({
      version: r.version,
      date: typeof r.date === 'string' ? r.date : undefined,
      notes_url: typeof r.notes_url === 'string' ? r.notes_url : undefined,
      highlights: Array.isArray(r.highlights) ? r.highlights.filter((h) => typeof h === 'string') : [],
    }));
  const how = prod?.how_to_update?.[platform];
  return {
    latest,
    releases: listed,
    howToUpdate: typeof how === 'string' ? how : null,
    downloadUrl: typeof prod?.download_url === 'string' ? prod.download_url : null,
  };
}
