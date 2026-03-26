//===-- PluginLoader.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_PLUGINLOADER_H
#define KLEE_PLUGINLOADER_H

#include <string>
#include <vector>

struct KleePluginHandlerInfo;

namespace klee {

class SpecialFunctionHandler;

/// Load a plugin shared library and register its handlers.
/// Returns true on success, false on failure (with klee_warning).
bool loadPlugin(const std::string &path,
                std::vector<const KleePluginHandlerInfo *> &allInfos);

} // namespace klee

#endif /* KLEE_PLUGINLOADER_H */
