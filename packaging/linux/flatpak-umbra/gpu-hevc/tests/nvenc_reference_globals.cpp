// SPDX-License-Identifier: GPL-3.0-only
// Only Apollo's logging globals are supplied here. No configuration parser,
// service, network stack, display control, or encoder behavior is replaced.
#include "src/logging.h"

boost::log::sources::severity_logger<int> verbose(0);
boost::log::sources::severity_logger<int> debug(1);
boost::log::sources::severity_logger<int> info(2);
boost::log::sources::severity_logger<int> warning(3);
boost::log::sources::severity_logger<int> error(4);
boost::log::sources::severity_logger<int> fatal(5);
namespace config {
    // Used by the header-only periodic logger; explicit NVENC/session config
    // comes from the strict benchmark manifest, not this logging-only object.
    sunshine_t sunshine{};
}
