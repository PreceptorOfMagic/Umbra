/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
namespace platf {
  bool portal_coop_supported();
  bool portal_prepare_virtual(int width, int height, int milli_hz);
  bool portal_virtual_active();
  void portal_release_virtual();
}
