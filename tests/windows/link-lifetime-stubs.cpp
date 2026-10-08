// Harmless lifetime fixture: compile with the real aurora_link/main.cpp.
// No sockets, controller drivers, audio devices or session hooks are exercised.
#include <windows.h>
#include "src/aurora_link/aurora_link.h"
namespace aurora_link {
bool set_cable_enabled(bool) { return true; }
bool rename_endpoints(const std::vector<std::pair<std::wstring,std::wstring>> &) { return true; }
int run_pad_host() { while (!helper_stopping()) Sleep(10); return 0; }
int run_mic_host() { while (!helper_stopping()) Sleep(10); return 0; }
bool save_default_endpoint(const std::wstring &) { return true; }
bool restore_default_endpoint(const std::wstring &) { return true; }
std::wstring session_audio_snapshot() { return {}; }
std::wstring widen(const char *) { return {}; }
int session_start() { return 0; }
int session_end() { return 0; }
}
