/**
 * @file src/beta_profile.h
 * @brief PER-CLIENT CONFIG OVERRIDES FOR THE BETA AURORA CLIENT. **BETA-ONLY — DELETE FOR RELEASE.**
 *
 * WHY THIS EXISTS. Aurora ships as two installs on the TV — `com.aurora.gamestream` (live) and
 * `com.aurora.gamestream.beta` — because webOS namespaces an app's config by its id, so the beta
 * VEGA/AVC co-op work cannot disturb the live install. Apollo has no equivalent: it is a Windows
 * service with fixed ports, one capture device and one virtual display, so **two Apollo instances
 * cannot run at once on one host**. A host that serves both clients therefore needs its ONE Apollo
 * to answer each with different settings.
 *
 * WHAT IDENTIFIES A CLIENT. Nothing was added to the protocol. The beta app has its own `conf/`,
 * so it pairs as a SEPARATE client with its own certificate and its own name, and Apollo already
 * carries that name per session as `launch_session_t::device_name` (`nvhttp.cpp`). Matching on it
 * is enough.
 *
 * SCOPE, deliberately narrow:
 *   - Overrides apply for the DURATION OF ONE SESSION and are restored when it ends. They never
 *     touch `sunshine.conf`, so nothing survives a crash or a restart into the live client's lap.
 *   - Only settings READ AT SESSION OR ENCODER SETUP can work this way. `gop_length` (video.cpp)
 *     and `coop_pad_rows` (process.cpp) both qualify. A setting consumed once at SERVICE START
 *     cannot be switched per session at all — do not add one here and assume it took.
 *   - **VEGA MUST NOT HAVE A `beta_profile.conf`.** It serves the beta client only, so its normal
 *     `sunshine.conf` already holds the beta values and there is nothing to switch. Absent file =
 *     this module is completely inert, which is also the state every host is in until someone
 *     writes one.
 *
 * HOW TO REMOVE IT FOR RELEASE: delete this file and `beta_profile.cpp`, drop the two `#ifdef
 * APOLLO_BETA_PROFILE` hooks in `nvhttp.cpp` and `rtsp.cpp`, and drop the source entry and the
 * compile definition in `cmake/compile_definitions/common.cmake`. Building without
 * `APOLLO_BETA_PROFILE` already compiles all of it out, so a release build carries none of it
 * even before the files are deleted.
 */
#pragma once

#include <string>

namespace beta_profile {

  /**
   * @brief Apply this client's overrides, if it is a configured beta client.
   * @param client_name `launch_session_t::device_name` — the PAIRED CERTIFICATE's name.
   * @param client_uuid `launch_session_t::unique_id` — that certificate's uuid.
   *
   * MATCH ON THE UUID, NOT THE NAME, UNLESS YOU HAVE CHECKED THE NAME IS DISTINCT. Measured
   * 2026-09-07 on SKYNET: the only paired client is named "Legacy Moonlight Client" — a generic
   * name Aurora does not override, so the beta app would very likely pair under the SAME name and
   * name-matching would silently never fire. The uuid is per-certificate and the beta app has its
   * own conf/, so it is guaranteed distinct.
   * @return true if overrides were applied (and must later be restored).
   *
   * Safe to call when no `beta_profile.conf` exists: it returns false and changes nothing.
   * Calling it twice without an intervening restore restores first, so a session that never
   * cleanly ended cannot leak its overrides into the next one.
   */
  bool apply_for_client(const std::string &client_name, const std::string &client_uuid);

  /**
   * @brief Put back whatever `apply_for_client` changed. Idempotent; safe if nothing was applied.
   */
  void restore();

}  // namespace beta_profile
