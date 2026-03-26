//===-- PluginHandler.h -----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Public interface for KLEE external function handler plugins.
//
// A plugin is a shared library (.so/.dylib) exporting klee_plugin_get_handlers()
// which returns a null-terminated array of handler descriptors.
//
// Usage: klee --load-plugin=./my_handlers.so program.bc
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_PLUGINHANDLER_H
#define KLEE_PLUGINHANDLER_H

// Opaque types — plugins don't need KLEE internals to declare handlers.
// The actual types are resolved when the plugin is loaded into KLEE.
struct KleePluginHandlerArgs;

/// Handler function signature. The void* args pointer is cast to the
/// internal handler arguments by KLEE's dispatch code.
/// Plugins that want full access should include KLEE internal headers.
typedef void (*KleePluginHandlerFn)(void *sfh, void *state,
                                    void *target, void *arguments);

/// Descriptor for a single plugin handler.
struct KleePluginHandlerInfo {
  const char *functionName; ///< Function to intercept
  KleePluginHandlerFn handler;
  bool doesNotReturn;
  bool hasReturnValue;
};

/// Symbol name for the plugin's registration function.
#define KLEE_PLUGIN_GET_HANDLERS_SYMBOL "klee_plugin_get_handlers"
typedef const KleePluginHandlerInfo *(*KleePluginGetHandlersFn)();

#endif /* KLEE_PLUGINHANDLER_H */
