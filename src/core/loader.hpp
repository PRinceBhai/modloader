/*
 * Copyright (C) 2013-2014  LINK/2012 <dma_2012@hotmail.com>
 * Licensed under GNU GPL v3, see LICENSE at top level directory.
 * 
 */
#ifndef LOADER_HPP
#define	LOADER_HPP

#define __STDC_FORMAT_MACROS
#include <cinttypes>
#include <modloader.hpp>
#include <modloader_util_path.hpp>
#include <modloader_util_container.hpp>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>

using namespace modloader;

extern class Loader loader;

// URL
static const char* modurl  = "https://github.com/thelink2012/modloader";
static const char* downurl = "https://github.com/thelink2012/modloader/releases";


// Functor for sorting based on priority
template<class T>
struct SimplePriorityPred
{
    bool operator()(const T& a, const T& b) const
    {
        return (a.priority > b.priority);    // That's right, higher priority means lower
    }
};

// Functor for sorting based on priority and name
template<class T>
struct PriorityPred
{
    SimplePriorityPred<T> pred;

    bool operator()(const T& a, const T& b) const
    {
        return (a.priority != b.priority?
                pred(a, b) : compare(a.name, b.name, true) < 0);
    }
};





// The Mod Loader Core
class Loader : public modloader_t
{
    public:
        static const int default_priority = 50;         // Default priority for mods
        static const int default_cmd_priority = 80;     // Default priority for mods sent by command line
        
        // Forwarding declarations
        class ModInformation;
        class FileInformation;
        class PluginInformation;
        class FolderInformation;
        typedef std::map<std::string, ref_list<PluginInformation>> ExtMap;
        
        // File flags
        enum class FileFlags : decltype(modloader::file::flags)
        {
            None        = 0,
            IsDirectory = MODLOADER_FF_IS_DIRECTORY,    // File is a directory
        };
        
        // Status for a mod object
        enum class Status : uint8_t
        {
            Unchanged,          // Unchanged since last install
            Added,              // Added to filesystem since last install
            Updated,            // Updated at filesystem since last install
            Removed,            // Removed from the filesystem since last install
        };
        
        // Behaviour for a file
        enum class BehaviourType
        {
            No      = MODLOADER_BEHAVIOUR_NO,
            Yes     = MODLOADER_BEHAVIOUR_YES,
            CallMe  = MODLOADER_BEHAVIOUR_CALLME
        };
        
        
        
        
        
        // Information about a Mod Loader plugin
        class PluginInformation : public modloader::plugin 
        {
            protected:
                friend class Loader;
                typedef modloader::plugin base;
                
                // Plugin identifier (i.e. "gta/std/fx.dll" -> "gta.std.fx")
                std::string identifier;

                // All the behaviours being handled by this plugin
                std::map<uint64_t, FileInformation*> behv;
                
            public:
                PluginInformation(void* module, const char* modulename, modloader_fGetPluginData GetPluginData)
                {
                    // Fill basic information
                    std::memset(this, 0, sizeof(modloader::plugin));
                    this->pModule   = module;
                    this->modloader = &loader;
                    this->priority  = default_priority;
                    
                    // Fill the plugin structure with the rest of the informations
                    if(GetPluginData) GetPluginData(this);

                    //
                    this->identifier = NormalizePath(modulename);
                    std::replace(identifier.begin(), identifier.end(), cNormalizedSlash, '.');
                    this->identifier.erase(identifier.length() - 4);    // Remove '.dll' extension
                    this->name = identifier.c_str();                    // Identifier and name are the same

                    // Override priority
                    auto it = loader.plugins_priority.find(identifier);
                    if(it != loader.plugins_priority.end())
                    {
                         Log("\tOverriding priority, from %d to %d", priority, it->second);
                         this->priority = it->second;
                    }
                }
                
                
            protected:
                // Methods to install and uninstall files into this plugin
                friend class FileInformation;
                bool Install(FileInformation& file);
                bool Reinstall(FileInformation& file);
                bool Uninstall(FileInformation& file);
                
                
            protected:
                // Methods to deal with file behaviour
                BehaviourType FindBehaviour(modloader::file& m);
                FileInformation* FindFileWithBehaviour(uint64_t behaviour);
                
            private:
                // Methods mapping straight to the raw methods at modloader_file_t
                bool InstallFile(const modloader::file& m);
                bool ReinstallFile(const modloader::file& m);
                bool UninstallFile(const modloader::file& m);
                bool Startup();
                bool Shutdown();
                
                bool IsMainHandlerFor(const FileInformation& file)
                { return file.handler == this; }
                
                bool EnsureBehaviourPresent(const FileInformation& file);
        };
        
        
        
        // Information about a mod file
        class FileInformation : public modloader::file
        {
            protected:
                friend class Loader;
                ModInformation&                 parent;         // The mod this file belongs to
                PluginInformation*              handler;        // The plugin that will handle this file (may be null)
                std::string                     pathbuf;        // Path buffer (as used in base.buffer)
                ref_list<PluginInformation>     callme;         // Those plugins should receive this file, but they won't handle it
                bool                            installed;      // Is the mod installed?
                Status                          status;         // File status
                
            public:
                // Initializer
                FileInformation(ModInformation& parent, std::string&& xpathbuf, const modloader::file& m,
                                PluginInformation* xhandler, ref_list<PluginInformation>&& xcallme)
                
                    : parent(parent), handler(xhandler), pathbuf(std::move(xpathbuf)), callme(std::move(xcallme)),
                      installed(false), status(Status::Unchanged)
                {
                    std::memcpy(this, &m, sizeof(modloader::file));
                    modloader::file::parent = &parent;
                }
                
                // Checks if this file is installed
                bool IsInstalled() const { return installed; }
                    
                // Installs or uninstalls this file
                bool Install();
                bool Reinstall();
                bool Uninstall();

                // Updates the current file state based on another new state
                bool Update(const modloader::file& m);
        };
        
        
        // Information about a mod folder
        class ModInformation : public modloader::mod
        {
            protected:
                friend class Loader;
                friend struct PriorityPred<ModInformation>;
                FolderInformation&          parent;         // Owner of this mod
                std::string                 path;           // Path for this mod (relative to game dir), normalized
                std::string                 name;           // Name for this mod, this is the filename in path (normalized)
                std::string                 fs_name;        // Name for this mod on the filesystem (non normalized)
                std::map<std::string, FileInformation>  files; // Files inside this mod
                Status                      status;         // Mod status

            public:
                // Initializer
                ModInformation(std::string name, FolderInformation& parent, uint64_t id)
                    : fs_name(name), name(NormalizePath(name)), parent(parent), status(Status::Unchanged)
                {
                    this->id = id;
                    this->priority = parent.GetPriority(this->name);
                    MakeSureStringIsDirectory(this->path = parent.GetPath() + this->name);
                }
                
                // Scans this mod for new, updated or removed files
                void Scan();
                
                // Uninstall / Install files after scanning and finding out the status of mods
                void ExtinguishNecessaryFiles();
                void InstallNecessaryFiles();
                
                const std::string& GetPath() { return this->path; }

            protected:
                const decltype(files)& InfoContainer() const { return files; }
                void SetUnchanged() { if(status != Status::Removed) status = Status::Unchanged; }
                //bool NeedsToBeCollected() const { return this->status == Status::Removed; }
                //bool CannotCollectBecauseOfFiles() const { return this->status == Status::Removed && !files.empty(); }
        };
        
        
        
        // Information about a modloader folder
        class FolderInformation
        {
            public:
                typedef std::map<std::string, FolderInformation>    FolderInformationList;
                typedef std::map<std::string, ModInformation>       ModInformationList;

            public:
                FolderInformation(const std::string& path, FolderInformation* parent = nullptr)
                    : path(path + cNormalizedSlash), parent(parent), status(Status::Unchanged)
                {}
                
                // Ignore checking
                bool IsIgnored(const std::string& name);
                bool IsFileIgnored(const std::string& name);
                
                // Adders (and Finders)
                FolderInformation& AddChild(const std::string& path);
                ModInformation& AddMod(const std::string& name);
                
                // Priority, inclusion and ignores
                void SetPriority(std::string name, int priority);
                int GetPriority(const std::string& name);
                void Include(std::string name);
                void IgnoreFileGlob(std::string glob);
                
                // Sets flags
                void SetIgnoreAll(bool bSet);
                void SetExcludeAll(bool bSet);
                void SetForceExclude(bool bSet);

                // Gets me, my childs, my child-childs....
                ref_list<FolderInformation> GetAll();
                
                // Get my mods sorted by priority
                ref_list<ModInformation>    GetModsByPriority();
                
                // Rebuilds the glob string for exclude_globs
                void RebuildIncludeModsGlob();
                void RebuildExcludeFilesGlob();
                
                // Scanning and Updating
                void Scan();
                void Update();  // After this call some ModInformation may have been deleted
                static void Update(ModInformation& mod);    // ^
                
                // Gets the path to this modfolder (relative to gamedir, normalized)
                const std::string& GetPath() { return path; }
                
                // Clears all buffers from this structure
                void Clear();
                
            protected:
                friend class Loader;
                Status status;                      // Folder status
                
            private:
                std::string path;                   // Path relative to game dir
                FolderInformation* parent;          // Parent folder
                
                ModInformationList       mods;      // All mods on this folder
                FolderInformationList    childs;    // All child mod folders on this mod folder (sub sub "modloader/" folders)
                
                // List of settings, all strings are normalized!!!!!
                std::map<std::string, int> mods_priority;   // List of priorities to be applied to mods
                std::set<std::string> include_mods;         // All mod globs inside this list shall be included when bExcludeAll is true
                std::set<std::string> exclude_files;        // All file globs inside this list shall be ignored
                std::string include_mods_glob;              // include_mods built into a single glob
                std::string exclude_files_glob;             // exclude_files built into a single glob
                
                // Folder flags
                struct flags_t
                {
                    bool bIgnoreAll;        // When true, no mod will be readen
                    bool bExcludeAll;       // When true, no mod gets loaded but the ones at include_mods list (set by INI)
                    bool bForceExclude;     // When true, have the same effect as exclude all (set by command line)
                    
                    // Defaults
                    flags_t() : bIgnoreAll(false), bExcludeAll(false), bForceExclude(false)
                    {}
                    
                } flags;
                
            protected:
                const decltype(childs)& InfoContainer() const { return childs; }
                void SetUnchanged() { if(status != Status::Removed) status = Status::Unchanged; }
                //bool NeedsToBeCollected() const { return this->status == Status::Removed; }
                //bool CannotCollectBecauseOfFiles() const { return this->status == Status::Removed && !mods.empty(); }
        };

        
    protected:
        friend struct scoped_gdir;
        
        // Configs
        uint64_t        maxBytesInLog;          // Maximum number of bytes in the logging stream 
        bool            bRunning;               // True when the loader was started up, false otherwise
        bool            bEnableLog;             // Enable logging to the log file
        bool            bImmediateFlush;        // Enable immediately flushing the log file
        bool            bEnablePlugins;         // Enable the loading of ML plugins
        
        uint64_t        numBytesInLog;
        
        // Unique ids
        uint64_t        currentModId;           // Current id for the unique mod id
        uint64_t        currentFileId;          // Current id for the unique file id (hibit is set)
        
        // Directories
        std::string     gamePath;               // Full game path
        std::string     cachePath;              // Cache path (relative to game path)
        std::string     pluginPath;             // Plugins path (relative to game path)
        
        // Modifications and Plugins
        FolderInformation               mods;               // All mods are contained on this folder
        ExtMap                          extMap;             // List of extensions and the plugins that takes care of it
        std::map<std::string, int>      plugins_priority;   // List of priorities to be applied to plugins
        std::list<PluginInformation>    plugins;            // List of plugins
        
    private: // Logging
        void OpenLog();     // Open log stream
        void CloseLog();    // Closes log stream
        void TruncateLog();
 
    private: // Plugins Management
        
        bool StartupPlugin(PluginInformation& plugin);
        
        // Loads / unloads all plugins
        void LoadPlugins();
        void UnloadPlugins();
        
        // Loads / Unloads plugins during run-time
        bool LoadPlugin(std::string filename);
        bool UnloadPlugin(PluginInformation& plugin);

        // Rebuilds the extMap object
        void RebuildExtensionMap();
        ref_list<PluginInformation> GetPluginsBy(const std::string& extension);
        
    private: // Basic configuration
        void ReadBasicConfig(const char*);
        void ParseCommandLine();
        
    public:
        
        // Constructor
        Loader() : mods("modloader")
        {}
        
        // Patches the game code to run this core
        void Patch();
        
        // Start or Shutdown the loader
        void Startup();
        void Shutdown();
        
        // Called on specific game circustances
        void OnReload();    // On Game Reloading
        
        // Logging functions
        static void LogGameVersion();
        static void Log(const char* msg, ...);
        static void vLog(const char* msg, va_list va);
        static void Error(const char* msg, ...);
        static void FatalError(const char* msg, ...);
        
        // Unique ids function
        uint64_t PickUniqueModId()  { return ++currentModId; }
        uint64_t PickUniqueFileId() { return ++currentFileId; }
        
        
        // Scan and update (install) the mods
        void ScanAndUpdate();
        
        // Finds the plugin that'll handle the file @m, or that needs to get called (@out_callme)
        PluginInformation* FindHandlerForFile(modloader::file& m, ref_list<PluginInformation>& out_callme);
        
        
        
        // Marks all status at the specified @map to @status
        template<class M>
        static void MarkStatus(M& map, Loader::Status status)
        {
            for(auto& pair : map) pair.second.status = status;
        }


        // Updates the status of @info based on the content of @map, after a full rescan happened
        // If the mod has been deleted @fine should be false, otherwise true
        template<class T, class M>
        static void UpdateStatus(T& info, M& map, bool fine)
        {
            if(!fine)
            {
                // Failed to scan info, it probably has been deleted
                info.status = Status::Removed;
            }
            else if(info.status != Status::Added)   // is Updated or Unchanged?
            {
                info.status = Status::Unchanged;
                
                for(auto& pair : map)
                {
                    if(pair.second.status != Status::Unchanged)   // status is Updated?
                    {
                        // Something changed here on this scan
                        info.status = Status::Updated;
                        break;
                    }
                }
            }
        }
        
        // Collects garbage information at the map @self
        template<class I>
        static void CollectInformation(I& self)
        {
            typedef typename I::mapped_type T;
            auto NeedsCollect = [&](const T& item) { return item.status == Status::Removed; };
            auto CanCollect   = [&](const T& item) { return NeedsCollect(item) && item.InfoContainer().empty(); };

            for(auto it = self.begin(); it != self.end(); )
            {
                auto& item = it->second;
                const char* path = item.GetPath().c_str();
                
                if(CanCollect(item))
                {
                    Log("Collecting '%s'", path);
                    it = self.erase(it);
                }
                else
                {
                    if(NeedsCollect(item)) Log("Cannot collect '%s' because of remaining files!", path);
                    ++it;
                }
            }
        }
        
        friend void test();
};




// Plugins are equal only (and only if) they point to the same data space
inline bool operator==(const Loader::PluginInformation& a, const Loader::PluginInformation& b)
{
    return (&a == &b);
}

// Scoped chdir relative to gamedir
struct scoped_gdir : public modloader::scoped_chdir
{
    scoped_gdir(const char* newdir) : scoped_chdir((!newdir[0]? loader.gamePath : loader.gamePath + newdir).data())
    { }
};





#endif
