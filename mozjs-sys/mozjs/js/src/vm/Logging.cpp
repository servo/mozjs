/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/Logging.h"

// Initialize all LogModules to speak with the provided interface.
/* static */ bool js::LogModule::initializeAll(
    const JS::LoggingInterface iface) {
#define INITIALIZE_MODULE(X, HELP) X##Module.initialize(iface);

  FOR_EACH_JS_LOG_MODULE(INITIALIZE_MODULE)

#undef INITIALIZE_MODULE

  return true;
}
