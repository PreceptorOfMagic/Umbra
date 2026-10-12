/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
namespace platf {
  bool portal_capture_active();
  bool portal_coop_supported();
  bool portal_kde_native_supported();
  bool portal_physical_hdr_active();
  void portal_physical_capture(bool on);
  bool portal_physical_capture_requested();
  bool portal_prepare_virtual(int width, int height, int milli_hz, bool hdr);
  bool portal_virtual_hdr_supported();
  void portal_probe_virtual_hdr();
  bool portal_virtual_active();
  void portal_release_virtual();
  void portal_restore_outputs();
}
