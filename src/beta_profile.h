/**
 * @file src/beta_profile.h
 * @brief Per-client encoder/display settings for Eclipse Beta. BETA-ONLY.
 *
 * The paired certificate UUID selects beta_profile.conf beside sunshine.conf. The
 * file never changes sunshine.conf. Only settings consumed at display/encoder setup
 * belong here: gop_length, coop_pad_rows and nvenc_h264_main. The last selects NVENC
 * Main profile for mixed AVC; ordinary NVIDIA sessions retain their configured profile.
 *
 * A lease must be acquired before display setup and encoder probing. HTTP failures
 * and pending-launch timeouts release it; a successful stream holds it until its
 * encoder threads have joined. Clearing the RTSP handshake is NOT stream teardown.
 * Incompatible simultaneous profiles are rejected instead of changing live globals.
 * Build without APOLLO_BETA_PROFILE to compile out this beta-only integration.
 */
#pragma once

#include <memory>
#include <string>

namespace beta_profile {
  struct scope_t;
  using lease_t = std::shared_ptr<scope_t>;

  // A pending launch is shared by HTTP cancel and RTSP allocation threads.
  inline lease_t retain(const lease_t &slot) {
    return std::atomic_load(&slot);
  }
  inline void release(lease_t &slot) {
    std::atomic_store(&slot, lease_t {});
  }

  /**
   * Acquire the settings for a paired certificate. UUID matching is preferred:
   * Eclipse's live and beta certificates can share the same generic client name.
   * An absent file or unmatched client uses normal settings. Compatible sessions
   * share a lease; an incompatible active profile or invalid matching config throws
   * std::runtime_error without modifying the current settings. Releasing the last
   * owner restores exactly the values changed, including on launch failure.
   */
  lease_t acquire_for_client(const std::string &client_name, const std::string &client_uuid);
}  // namespace beta_profile
