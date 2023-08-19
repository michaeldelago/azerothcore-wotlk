#ifndef SCRIPT_RELOADER_H
#define SCRIPT_RELOADER_H

#include "Define.h"
#include <memory>
#include <string>
#include <filesystem>

/// Represents a strong reference to a dynamic library which
/// provides C++ scripts. As long as one reference to the library exists
/// the library is kept loaded in the server, which makes it possible to lazy
/// unload several script types on demand (like SpellScripts), and to
/// provide multiple versions of the same script to the script factories.
///
/// Acquire a new reference through using:
/// `DynamicModuleLoader::AcquireModuleReferenceOfContext`
class ModuleReference
{
public:
    virtual ~ModuleReference() { }

    /// Returns the git revision hash of the referenced script module
    virtual char const* GetScriptModuleRevisionHash() const = 0;
    /// Returns the name of the referenced script module
    virtual char const* GetScriptModule() const = 0;
    /// Returns the path to the script module
    virtual std::filesystem::path const& GetModulePath() const = 0;
};

/// Provides the whole physical dynamic library unloading capability.
/// Loads, Reloads and Unloads dynamic libraries on changes and
/// informs the ScriptMgr about changes which were made.
/// The DynamicModuleLoader is also responsible for watching the source directory
/// and to invoke a build on changes.
class AC_GAME_API DynamicModuleLoader
{
protected:
    DynamicModuleLoader() { }

public:
    virtual ~DynamicModuleLoader() { }

    /// Initializes the DynamicModuleLoader
    virtual void Initialize() { }

    /// Needs to be called periodically to check for updates on script modules.
    /// Expects to be invoked in a thread safe way which means it's required that
    /// the current thread is the only one which accesses the world data.
    virtual void Update() { }

    /// Unloads the DynamicModuleLoader
    virtual void Unload() { }

    /// Returns an owning reference to the current module of the given context
    static std::shared_ptr<ModuleReference> AcquireModuleReferenceOfContext(
        std::string const& context);

    /// Returns the unique DynamicModuleLoader singleton instance
    static DynamicModuleLoader* instance();
};

#define sDynamicModuleLoader DynamicModuleLoader::instance()

#endif // DYNAMIC_MODULE_LOADER_H
