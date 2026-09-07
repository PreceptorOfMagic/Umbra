/**
 * @file src/aurora_link/main.cpp
 * @brief Aurora Link entry point — subcommand dispatch.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "aurora_link.h"

#pragma comment(lib, "ws2_32.lib")

namespace aurora_link {

  std::string agent_session(bool on) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      return {};
    }
    std::string reply;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
      // The agent is always local: it bridges devices into THIS machine's USB stack.
      sockaddr_in addr {};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(agent_port);
      InetPtonW(AF_INET, L"127.0.0.1", &addr.sin_addr);

      DWORD timeout = 3000;
      setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));

      if (connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
        const std::string cmd = on ? "SESSION ON\n" : "SESSION OFF\n";
        send(s, cmd.c_str(), static_cast<int>(cmd.size()), 0);
        char buf[256] = {};
        int n = recv(s, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
          reply.assign(buf, static_cast<size_t>(n));
        }
      } else {
        // Not fatal: a host with no CTM agent (PC2 today) still streams, it just
        // has no bridged controllers. Saying so beats failing the session.
        fprintf(stderr, "aurora-link: no CTM agent on 127.0.0.1:%u\n", agent_port);
      }
      closesocket(s);
    }

    WSACleanup();
    return reply;
  }

}  // namespace aurora_link

namespace {

  int usage() {
    fprintf(stderr,
            "aurora-link — Apollo host companion\n"
            "\n"
            "  aurora-link cable on|off      show/hide the virtual audio cable (admin)\n"
            "  aurora-link session on|off    open/close the CTM bridge gate\n"
            "  aurora-link rename-audio      give the virtual cable meaningful names\n"
            "  aurora-link run               run the pad host (UDP -> ViGEm)\n"
            "  aurora-link mic               run the mic host (Opus/RTP -> virtual cable)\n"
            "  aurora-link audio-default save|restore   remember/put back the default sink\n"
            "  aurora-link session-start     everything a stream needs, as one verb\n"
            "  aurora-link session-end       ...and put it all back\n");
    return 2;
  }

  bool is_on(const char *s) {
    return s != nullptr && (_stricmp(s, "on") == 0 || _stricmp(s, "start") == 0 || _stricmp(s, "1") == 0);
  }

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    return usage();
  }

  const char *verb = argv[1];

  if (_stricmp(verb, "cable") == 0) {
    if (argc < 3) {
      return usage();
    }
    return aurora_link::set_cable_enabled(is_on(argv[2])) ? 0 : 1;
  }

  if (_stricmp(verb, "session") == 0) {
    if (argc < 3) {
      return usage();
    }
    const std::string reply = aurora_link::agent_session(is_on(argv[2]));
    if (!reply.empty()) {
      printf("%s", reply.c_str());
    }
    return 0;
  }

  if (_stricmp(verb, "rename-audio") == 0) {
    // Applied at install time and on every session start, so a device that was
    // absent (the cable is hidden between sessions) still ends up named correctly.
    return aurora_link::rename_endpoints({
             {L"CABLE Input", L"Aurora Mic Bridge"},
             {L"CABLE In 16ch", L"Aurora Mic Bridge 16ch"},
             {L"CABLE Output", L"Aurora Mic"},
           }) ?
             0 :
             1;
  }

  if (_stricmp(verb, "run") == 0) {
    return aurora_link::run_pad_host();
  }

  if (_stricmp(verb, "mic") == 0) {
    return aurora_link::run_mic_host();
  }

  if (_stricmp(verb, "audio-default") == 0) {
    if (argc < 3) {
      return usage();
    }
    // The path is optional: with none given, use the same per-session scratch file
    // session-start/-end use, which is the only case that matters in production.
    const std::wstring path = argc > 3 ? aurora_link::widen(argv[3]) : aurora_link::session_audio_snapshot();
    if (_stricmp(argv[2], "save") == 0) {
      return aurora_link::save_default_endpoint(path) ? 0 : 1;
    }
    if (_stricmp(argv[2], "restore") == 0) {
      return aurora_link::restore_default_endpoint(path) ? 0 : 1;
    }
    return usage();
  }

  // session-start / session-end are what Apollo's global_prep_cmd names. They must
  // never return non-zero: Apollo aborts the stream on a failed prep-cmd, and none
  // of this is worth refusing to play a game over.
  if (_stricmp(verb, "session-start") == 0) {
    return aurora_link::session_start();
  }

  if (_stricmp(verb, "session-end") == 0) {
    return aurora_link::session_end();
  }

  return usage();
}
