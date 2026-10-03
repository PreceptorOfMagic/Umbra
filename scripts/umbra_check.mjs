#!/usr/bin/env node
// Umbra's delta check: how far this tree is from the Apollo it is built on.
//
//   node scripts/umbra_check.mjs [apollo-ref]      (default: origin/master)
//
// Umbra follows Apollo's releases, so every edit to an Apollo file is a place a future merge can
// conflict (docs/umbra-upstream-sync.md). This fails when:
//   1. an Apollo file Umbra must never edit differs from Apollo (README.md, the translations, the
//      resource/client cards) -- identity for those lives in umbra_identity.js / UmbraCard.vue;
//   2. an Apollo file NOT listed in scripts/umbra-delta.txt differs -- add it there, with its
//      reason, only if the change cannot live in a file of Umbra's own;
//   3. one of the identity hooks is missing (they are what make the rest of the UI say Umbra).
// It prints every Apollo file Umbra changes, with line counts, so the delta stays visible.

import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const git = (...args) => execFileSync("git", ["-C", root, ...args], { encoding: "utf8" }).trim();
const apolloRef = process.argv[2] || "origin/master";
const base = git("merge-base", "HEAD", apolloRef);
const errors = [];

const NEVER_EDIT = [
  /^README\.md$/,
  /^src_assets\/common\/assets\/web\/public\/assets\/locale\/[^/]+\.json$/,
  /^src_assets\/common\/assets\/web\/(ResourceCard|ClientCard)\.vue$/,
];

const allowed = new Set(
  fs.readFileSync(path.join(root, "scripts/umbra-delta.txt"), "utf8")
    .split(/\r?\n/).map((l) => l.replace(/#.*/, "").trim()).filter(Boolean),
);

// Apollo files = files present at the merge base. Compare the WORKING TREE, so uncommitted edits count.
const changed = git("diff", "--numstat", "--no-renames", "--diff-filter=MD", base, "--").split("\n").filter(Boolean)
  .map((l) => { const [a, d, f] = l.split("\t"); return { f, a, d }; });

for (const { f } of changed) {
  if (NEVER_EDIT.some((re) => re.test(f))) errors.push(`${f}: Apollo file Umbra must not edit (use umbra_identity.js / UmbraCard.vue)`);
  else if (!allowed.has(f)) errors.push(`${f}: Apollo file changed but not listed in scripts/umbra-delta.txt`);
}
for (const f of allowed) {
  if (!changed.some((c) => c.f === f)) console.log(`note: ${f} is listed in umbra-delta.txt but no longer differs -- drop it`);
}

function require(file, pattern, message) {
  const p = path.join(root, file);
  if (!fs.existsSync(p) || !pattern.test(fs.readFileSync(p, "utf8"))) errors.push(`${file}: ${message}`);
}
require("src_assets/common/assets/web/locale.js", /applyUmbraIdentity\(messages\)/, "text-layer hook missing");
require("src_assets/common/assets/web/template_header.html", /assets\/css\/umbra\.css/, "colour-scheme link missing");
require("src_assets/common/assets/web/index.html", /<Umbra-Card>/, "Umbra card missing from the home page");
require("cmake/packaging/windows.cmake", /CPACK_PACKAGE_INSTALL_DIRECTORY "Apollo"/, "install folder must stay Apollo (in-place upgrade)");
require("src_assets/windows/misc/service/install-service.bat", /DisplayName= "Umbra Host Service"/, "service display name missing");
require("src_assets/windows/misc/service/install-service.bat", /set SERVICE_NAME=ApolloService/, "service key must stay ApolloService (in-place upgrade)");
require("src/system_tray.cpp", /Open Umbra/, "tray menu does not name Umbra");
require("CMakeLists.txt", /project\(Umbra\b/, "project name is not Umbra");

const rows = changed.map(({ f, a, d }) => `${(a === "-" ? "bin" : `+${a}/-${d}`).padStart(10)}  ${f}`);
console.log(`Umbra vs Apollo ${git("describe", "--tags", "--always", base)} (${base.slice(0, 8)}): ${changed.length} Apollo files changed`);
for (const r of rows) console.log(r);
if (errors.length) {
  console.error(`\n${errors.length} problem(s):`);
  for (const e of errors) console.error(`  ${e}`);
  process.exit(1);
}
console.log("\nOK");
