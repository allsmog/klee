//===-- PluginLoader.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PluginLoader.h"

#include "klee/Core/PluginHandler.h"
#include "klee/Support/ErrorHandling.h"

#include <dlfcn.h>

namespace klee {

bool loadPlugin(const std::string &path,
                std::vector<const KleePluginHandlerInfo *> &allInfos) {
  void *handle = dlopen(path.c_str(), RTLD_NOW);
  if (!handle) {
    klee_warning("Failed to load plugin %s: %s", path.c_str(), dlerror());
    return false;
  }

  auto getHandlers = reinterpret_cast<KleePluginGetHandlersFn>(
      dlsym(handle, KLEE_PLUGIN_GET_HANDLERS_SYMBOL));
  if (!getHandlers) {
    klee_warning("Plugin %s does not export %s: %s", path.c_str(),
                 KLEE_PLUGIN_GET_HANDLERS_SYMBOL, dlerror());
    dlclose(handle);
    return false;
  }

  const KleePluginHandlerInfo *infos = getHandlers();
  if (!infos || !infos->functionName) {
    klee_warning("Plugin %s returned no handlers", path.c_str());
    return true; // not an error, just empty
  }

  allInfos.push_back(infos);
  unsigned count = 0;
  for (const KleePluginHandlerInfo *p = infos; p->functionName; p++)
    count++;
  klee_message("Loaded plugin %s with %u handler(s)", path.c_str(), count);
  return true;
}

} // namespace klee
