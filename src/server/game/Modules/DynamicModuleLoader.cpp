/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright
 * information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "DynamicModuleLoader.h"
#include <Log.h>
#include <Optional.h>
#include <algorithm>
#include <boost/dll/runtime_symbol_info.hpp>
#include <filesystem>
#include <format>
#include <regex>
#include <type_traits>
#include <utility>
#include <string>

namespace fs = std::filesystem;

#if AC_PLATFORM == AC_PLATFORM_WINDOWS
#include <windows.h>
#define SHARED_LIBRARY_PREFIX ""
#define SHARED_LIBRARY_EXTENSION "dll"
#define CLOSE_LIBRARY(handle) (FreeLibrary(handle) != 0)
#define GET_FUNCTION_FROM_SHARED(handle, name) GetProcAddress(handle, name)
#define LOAD_SHARED_LIBRARY(path) LoadLibrary(path)

typedef HMODULE HandleType;
#elif AC_PLATFORM == AC_PLATFORM_APPLE
#include <dlfcn.h>
#define SHARED_LIBRARY_PREFIX "libmod"
#define SHARED_LIBRARY_EXTENSION "dylib"
#define CLOSE_LIBRARY(handle) (dlclose(handle) == 0)
#define GET_FUNCTION_FROM_SHARED(handle, name) dlsym(handle, name)
#define LOAD_SHARED_LIBRARY(path) dlopen(path, RTLD_LAZY)

typedef void *HandleType;
#else // Posix
#include <dlfcn.h>
#define SHARED_LIBRARY_PREFIX "libmod"
#define SHARED_LIBRARY_EXTENSION "so"
#define CLOSE_LIBRARY(handle) (dlclose(handle) == 0)
#define GET_FUNCTION_FROM_SHARED(handle, name) dlsym(handle, name)
#define LOAD_SHARED_LIBRARY(path) dlopen(path, RTLD_LAZY)

typedef void *HandleType;
#endif

static boost::dll::fs::path GetDirectoryOfExecutable() {
  return boost::dll::program_location().parent_path();
}

class DynamicModuleUnloader {
public:
  explicit DynamicModuleUnloader(fs::path path) : path_(std::move(path)) {}
  void operator()(HandleType handle) const {
    bool success = CLOSE_LIBRARY(handle);

    if (!success) {
      LOG_ERROR("dynamicModule.loader",
                "Failed to unload (syscall) the shared library \"{}\"",
                path_.generic_string());
      return;
    }

    LOG_TRACE("dynamicModule.loader",
              "Lazy unloaded the shared library \"{}\".",
              path_.generic_string());
    }

private : fs::path const path_;
};

typedef std::unique_ptr<typename std::remove_pointer_t<HandleType>::type,
                        DynamicModuleUnloader>
    HandleHolder;

typedef char const *(*GetScriptModuleRevisionHashType)();
typedef void (*AddScriptsType)();
typedef char const *(*GetScriptModuleType)();
typedef char const *(*GetBuildDirectiveType)();

class DynamicModule : public ModuleReference {
public:
  explicit DynamicModule(
      HandlerHolder handle,
      GetScriptModuleRevisionHashType getScriptModuleRevisionHash,
      AddScriptsType addScripts, GetScriptModuleType getScriptModule,
      GetBuildDirectiveType getBuildDirective, fs::path const &path)
      : _handle(std::forward<HandleHolder>(handle)),
        _getScriptModuleRevisionHash(getScriptModuleRevisionHash),
        _addScripts(addScripts), _getScriptModule(getScriptModule),
        _getBuildDirective(getBuildDirective), _path(path) {}

  DynamicModule(DynamicModule const &) = delete;
  DynamicModule(DynamicModule &&right) = delete;

  DynamicModule &operator=(DynamicModule const &) = delete;
  DynamicModule &operator=(DynamicModule &&right) = delete;

  static Optional<std::shared_ptr<DynamicModule>>
  CreateFromPath(fs::path const &path);

  static void ScheduleDelayedDelete(DynamicModule *module);

  char const *GetScriptModuleRevisionHash() const override {
    return _getScriptModuleRevisionHash();
  }

  void AddScripts() const { return _addScripts(); }

  char const *GetScriptModule() const override { return _getScriptModule(); }

  char const *GetBuildDirective() const { return _getBuildDirective(); }

  fs::path const &GetModulePath() const override { return _path; }

private:
  HandleHolder _handle;

  GetScriptModuleRevisionHashType _getScriptModuleRevisionHash;
  AddScriptsType _addScripts;
  GetScriptModuleType _getScriptModule;
  GetBuildDirectiveType _getBuildDirective;

  fs::path _path;
};

template <typename Fn>
static bool GetFunctionFromSharedLibrary(HandleType handle,
                                         std::string const &name, Fn &fn) {
  fn = reinterpret_cast<Fn>(GET_FUNCTION_FROM_SHARED(handle, name.c_str()));

  return fn != nullptr;
}

Optional<std::shared_ptr<DynamicModule>> DynamicModule
    : CreateFromPath(fs::path const &path) {
  HandleType handle = LOAD_SHARED_LIBRARY(path.generic_string().c_str());

  if (!handle) {
      LOG_ERROR("dynamicModule.loader",
                "Could not load the shared library \"{}\"",
                path.generic_string());
    return {};
  }

  HandleHolder holder(handle,
                      DynamicModuleUnloader(path));

  GetScriptModuleRevisionHashType getScriptModuleRevisionHash;
  AddScriptsType addScripts;
  GetScriptModuleType getScriptModule;
  GetBuildDirectiveType getBuildDirective;

  if (GetFunctionFromSharedLibrary(handle, "GetScriptModuleRevisionHash",
                                   getScriptModuleRevisionHash) &&
      GetFunctionFromSharedLibrary(handle, "AddScripts", addScripts) &&
      GetFunctionFromSharedLibrary(handle, "GetScriptModule",
                                   getScriptModule) &&
      GetFunctionFromSharedLibrary(handle, "GetBuildDirective",
                                   getBuildDirective)) {
    auto module =
        new DynamicModule(std::move(holder), getScriptModuleRevisionHash,
                          addScripts, getScriptModule, getBuildDirective, path);

    return std::shared_ptr<DynamicModule>(module, ScheduleDelayedDelete);
  } else {
    LOG_ERROR("dynamicModule.loader",
              "Could not extract all required functions from the shared "
              "library \"{}\"!",
              path.generic_string());

    return {};
  }
}

static bool HasValidDynamicModuleName(std::string const &name) {
  static auto const pattern = std::regex(std::format(
      "{}_[a-zA-Z0-9_-]{}", SHARED_LIBRARY_PREFIX, SHARED_LIBRARY_EXTENSION));
  return std::regex_match(name, pattern);
}
