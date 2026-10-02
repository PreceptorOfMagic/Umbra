#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, "..");
const errors = [];

function read(relativePath) {
  return fs.readFileSync(path.join(repositoryRoot, relativePath), "utf8");
}

function requireMatch(relativePath, pattern, message) {
  if (!pattern.test(read(relativePath))) errors.push(`${relativePath}: ${message}`);
}

function rejectMatch(relativePath, pattern, message) {
  if (pattern.test(read(relativePath))) errors.push(`${relativePath}: ${message}`);
}

requireMatch("CMakeLists.txt", /project\(Umbra\b/, "public project/package name is not Umbra");
requireMatch("CMakeLists.txt", /PreceptorOfMagic\/umbra/, "project homepage is not the Umbra repository");
requireMatch("src/system_tray.cpp", /Open Umbra/, "tray does not name Umbra");
rejectMatch("src/system_tray.cpp", /Open Apollo/, "old tray branding remains");
requireMatch("src/network.cpp", /instancename : "Umbra"/, "default advertised host name is not Umbra");

requireMatch("src_assets/windows/misc/service/install-service.bat", /DisplayName= "Umbra Host Service"/, "visible service name is not Umbra Host Service");
requireMatch("src_assets/windows/misc/service/install-service.bat", /set SERVICE_NAME=ApolloService/, "compatibility service key changed without a migration");
requireMatch("src_assets/windows/misc/firewall/add-firewall-rule.bat", /set RULE_NAME=Umbra/, "current firewall rule is not Umbra");
requireMatch("src_assets/windows/misc/firewall/add-firewall-rule.bat", /LEGACY_RULE_NAME=Apollo/, "legacy firewall cleanup is missing");

for (const relativePath of [
  "src_assets/common/assets/web/template_header.html",
  "src_assets/common/assets/web/Navbar.vue",
  "src_assets/common/assets/web/configs/tabs/General.vue",
]) {
  requireMatch(relativePath, /Umbra/, "visible web identity does not name Umbra");
  rejectMatch(relativePath, /(?:title|alt|placeholder)="Apollo"|<title>Apollo<\/title>/, "visible Apollo branding remains");
}

const localeRoot = path.join(repositoryRoot, "src_assets", "common", "assets", "web", "public", "assets", "locale");
let localeCount = 0;
for (const filename of fs.readdirSync(localeRoot).filter((entry) => entry.endsWith(".json"))) {
  const relativePath = path.join("src_assets", "common", "assets", "web", "public", "assets", "locale", filename);
  const source = read(relativePath);
  try {
    JSON.parse(source);
  } catch (error) {
    errors.push(`${relativePath}: invalid JSON (${error.message})`);
  }
  if (/\bApollo\b/.test(source)) errors.push(`${relativePath}: visible Apollo branding remains in locale copy`);
  localeCount += 1;
}
if (localeCount === 0) errors.push("no web locale files were checked");

requireMatch("src_assets/common/assets/web/index.html", /PreceptorOfMagic\/umbra\/releases/, "update check does not use Umbra releases");
requireMatch("src_assets/common/assets/web/ResourceCard.vue", /umbra\/blob\/HEAD\/LICENSE/, "runtime licence link does not follow the repository default branch");
requireMatch("src_assets/common/assets/web/ResourceCard.vue", /umbra\/blob\/HEAD\/NOTICE/, "runtime notice link does not follow the repository default branch");
rejectMatch("src_assets/common/assets/web/ResourceCard.vue", /umbra\/blob\/main\//, "runtime legal link assumes a nonexistent main branch");
requireMatch("cmake/prep/options.cmake", /SUNSHINE_PUBLISHER_NAME "PreceptorOfMagic"/, "fork developer/publisher display still names an upstream party");
requireMatch("cmake/prep/options.cmake", /SUNSHINE_PUBLISHER_WEBSITE "https:\/\/github\.com\/PreceptorOfMagic"/, "fork developer website is not current");
requireMatch("cmake/packaging/common.cmake", /CPACK_PACKAGE_VENDOR \$\{SUNSHINE_PUBLISHER_NAME\}/, "package vendor is not derived from the fork developer setting");
requireMatch("cmake/packaging/common.cmake", /CPACK_PACKAGE_CONTACT \$\{SUNSHINE_PUBLISHER_WEBSITE\}/, "package contact is not derived from the fork developer setting");
requireMatch("src/platform/windows/windows.rc", /Umbra contributors; see LICENSE and NOTICE/, "Windows legal metadata does not identify Umbra contributors and local notices");
rejectMatch("src/platform/windows/windows.rc", /ClassicOldSong\/Apollo/, "Windows legal metadata still points to Apollo as publisher");
requireMatch("README.md", /github\.com\/SudoMaker\/SudoVDA/, "SudoVDA credit does not point to the actual driver project");
requireMatch("README.md", /Eclipse\/Umbra.*one project/s, "README does not establish the single-project hierarchy");
requireMatch("docs/building.md", /PreceptorOfMagic\/umbra\.git/, "build guide still clones the Apollo repository");
rejectMatch("docs/building.md", /git clone https:\/\/github\.com\/ClassicOldSong\/Apollo/, "build guide presents Apollo as the current host source");
requireMatch("src_assets/common/assets/web/welcome.html", /newUsername: "umbra"/, "first-run username still defaults to Apollo");
requireMatch(
  "src/aurora_link/mic.cpp",
  /\{L"Eclipse Mic Bridge", L"Aurora Mic Bridge", L"CABLE Input"\}/,
  "microphone lookup does not prefer Eclipse while retaining legacy and stock fallbacks",
);
for (const relativePath of [
  "apollo.svg",
  "src_assets/common/assets/web/public/images/logo-apollo.svg",
  "src_assets/common/assets/web/public/images/apollo-playing.svg",
  "src_assets/common/assets/web/public/images/apollo-pausing.svg",
  "src_assets/common/assets/web/public/images/apollo-locked.svg",
]) {
  requireMatch(relativePath, /Eclipse\/Umbra/, "visible artwork is not the shared Eclipse/Umbra mark");
  rejectMatch(relativePath, /apollo\.afdesign/, "old Apollo artwork remains in the visible icon source");
}
requireMatch("cmake/packaging/macos.cmake", /CPACK_BUNDLE_ICON "\$\{PROJECT_SOURCE_DIR\}\/apollo\.icns"/, "macOS package points to a missing legacy icon instead of the shared mark");

// Generated output is part of the identity audit when it exists. A green source
// tree must not hide an older Apollo-branded web bundle or installer configuration.
const generatedWebIndex = "build/assets/web/index.html";
if (fs.existsSync(path.join(repositoryRoot, generatedWebIndex))) {
  requireMatch(generatedWebIndex, /<title>Umbra<\/title>/, "generated web UI title is stale; rebuild the web assets");
  rejectMatch(generatedWebIndex, /<title>Apollo<\/title>/, "generated web UI still presents the host as Apollo");
}
const generatedCpack = "build/CPackConfig.cmake";
if (fs.existsSync(path.join(repositoryRoot, generatedCpack))) {
  for (const [pattern, message] of [
    [/set\(CPACK_NSIS_DISPLAY_NAME "Umbra"\)/, "generated installer display name is not Umbra"],
    [/set\(CPACK_NSIS_PACKAGE_NAME "Umbra"\)/, "generated NSIS package name is not Umbra"],
    [/set\(CPACK_PACKAGE_NAME "Umbra"\)/, "generated package name is not Umbra"],
    [/set\(CPACK_PACKAGE_HOMEPAGE_URL "https:\/\/github\.com\/PreceptorOfMagic\/umbra"\)/, "generated package homepage is stale"],
    [/set\(CPACK_PACKAGE_VENDOR "PreceptorOfMagic"\)/, "generated package vendor is stale"],
  ]) requireMatch(generatedCpack, pattern, message);
}
const generatedInstallerDirectory = path.join(repositoryRoot, "build", "cpack_artifacts");
if (fs.existsSync(generatedInstallerDirectory) && fs.readdirSync(generatedInstallerDirectory).some((name) => /^Apollo(?:[._-]|\.exe$)/i.test(name))) {
  errors.push("build/cpack_artifacts: stale Apollo-named installer remains; rebuild and remove it before release verification");
}

// These inherited Sunshine identifiers are deliberately retained under the naming rule.
requireMatch("package.json", /"name": "sunshine"/, "inherited Sunshine package identifier changed unexpectedly");
requireMatch("cmake/packaging/windows_nsis.cmake", /sunshine\.exe/, "inherited Sunshine executable name changed unexpectedly");

if (errors.length > 0) {
  console.error("Umbra public-identity check failed:");
  for (const error of errors) console.error(`- ${error}`);
  process.exit(1);
}

console.log(`Umbra public-identity check passed: ${localeCount} locale files and the principal visible surfaces use Umbra; inherited Sunshine identifiers remain.`);
