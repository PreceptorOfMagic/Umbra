/**
 * @file src/beta_profile.cpp
 * @brief Per-client config overrides for the beta Aurora client. BETA-ONLY — see beta_profile.h.
 */
#include "beta_profile.h"

#include <fstream>
#include <mutex>
#include <vector>

#include "config.h"
#include "logging.h"
#include "platform/common.h"

using namespace std::literals;  // the "..."sv literals below, as config.cpp does

namespace beta_profile {

  namespace {

    /**
     * THE OVERRIDABLE SET, and it is short ON PURPOSE.
     *
     * A setting may only live here if it is read at SESSION or ENCODER setup, i.e. after
     * `apply_for_client()` has run. Both of these are:
     *   gop_length     -> video.cpp, at encoder creation
     *   coop_pad_rows  -> process.cpp, at virtual-display creation
     *
     * Anything consumed once at SERVICE START cannot be switched per session and would sit here
     * looking effective while doing nothing — the exact failure this project has paid for
     * repeatedly. If you add a key, first find its read site and prove it runs per session.
     */
    struct knob_t {
      const char *name;
      int *target;
    };

    std::vector<knob_t> knobs() {
      return {
        {"gop_length", &config::video.gop_length},
        {"coop_pad_rows", &config::video.coop_pad_rows},
      };
    }

    std::mutex mtx;
    bool applied = false;
    std::vector<std::pair<int *, int>> saved;  // target -> original value

    std::string trim(std::string s) {
      const auto ws = " \t\r\n";
      const auto b = s.find_first_not_of(ws);
      if (b == std::string::npos) {
        return {};
      }
      return s.substr(b, s.find_last_not_of(ws) - b + 1);
    }

    /**
     * `beta_profile.conf`, beside sunshine.conf. Absent = this module is inert.
     *
     *     # every host that serves BOTH a live and a beta Aurora. NOT on VEGA.
     *     client_uuid = <the beta certificate's uuid>   # PREFERRED -- guaranteed distinct
     *     client = Aurora Beta                          # only if the name is actually distinct
     *     gop_length = 300
     *     coop_pad_rows = 16
     *
     * Either key may repeat; a session matches if EITHER its uuid or its name is listed.
     * PREFER `client_uuid`: measured 2026-09-07, SKYNET's only paired client is named
     * "Legacy Moonlight Client", so both Auroras would very likely share a name and
     * name-matching would silently never fire.
     */
    struct profile_t {
      std::vector<std::string> clients;
      std::vector<std::string> uuids;
      std::vector<std::pair<std::string, int>> values;
      bool loaded = false;
    };

    profile_t load() {
      profile_t p;
      const auto path = platf::appdata().string() + "/beta_profile.conf";
      std::ifstream in(path);
      if (!in.is_open()) {
        return p;  // the normal state on a live-only host, and on VEGA
      }
      p.loaded = true;
      std::string line;
      while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
          continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
          BOOST_LOG(warning) << "beta_profile: ignoring line without '=': "sv << line;
          continue;
        }
        const auto key = trim(line.substr(0, eq));
        const auto val = trim(line.substr(eq + 1));
        if (key == "client") {
          if (!val.empty()) {
            p.clients.push_back(val);
          }
          continue;
        }
        if (key == "client_uuid") {
          if (!val.empty()) {
            p.uuids.push_back(val);
          }
          continue;
        }
        try {
          p.values.emplace_back(key, std::stoi(val));
        } catch (...) {
          BOOST_LOG(warning) << "beta_profile: '"sv << key << "' needs an integer, got '"sv << val << "'"sv;
        }
      }
      return p;
    }

  }  // namespace

  bool apply_for_client(const std::string &client_name, const std::string &client_uuid) {
    std::lock_guard lock(mtx);

    // A session that never cleanly ended must not leak its overrides into the next one.
    if (applied) {
      BOOST_LOG(warning) << "beta_profile: overrides were still applied at the start of a new "
                            "session -- restoring before re-evaluating"sv;
      for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
        *it->first = it->second;
      }
      saved.clear();
      applied = false;
    }

    const auto p = load();
    if (!p.loaded) {
      return false;
    }

    bool match = false;
    for (const auto &u : p.uuids) {
      if (u == client_uuid) {
        match = true;
        break;
      }
    }
    for (const auto &c : p.clients) {
      if (!match && c == client_name) {
        match = true;
        break;
      }
    }
    if (!match) {
      // Print the uuid: it is what the conf should list, and it is otherwise hard to find.
      BOOST_LOG(info) << "beta_profile: client '"sv << client_name << "' uuid "sv << client_uuid
                      << " is not listed -- using this host's normal config"sv;
      return false;
    }

    const auto ks = knobs();
    for (const auto &[key, value] : p.values) {
      int *target = nullptr;
      for (const auto &k : ks) {
        if (key == k.name) {
          target = k.target;
          break;
        }
      }
      if (target == nullptr) {
        // Loud, because a silently-ignored override is indistinguishable from one that applied.
        BOOST_LOG(error) << "beta_profile: '"sv << key
                         << "' is NOT an overridable setting and was IGNORED. Only settings read "
                            "at session/encoder setup can be switched per session -- see "
                            "beta_profile.cpp knobs()."sv;
        continue;
      }
      saved.emplace_back(target, *target);
      BOOST_LOG(info) << "beta_profile: "sv << key << " "sv << *target << " -> "sv << value
                      << " for this session"sv;
      *target = value;
    }

    applied = !saved.empty();
    BOOST_LOG(info) << "beta_profile: BETA client '"sv << client_name << "' -- "sv << saved.size()
                    << " override(s) applied"sv;
    return applied;
  }

  void restore() {
    std::lock_guard lock(mtx);
    if (!applied) {
      return;
    }
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
      *it->first = it->second;
    }
    BOOST_LOG(info) << "beta_profile: "sv << saved.size() << " override(s) restored at session end"sv;
    saved.clear();
    applied = false;
  }

}  // namespace beta_profile
