// Copyright (C) 2017-2018 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

// replace with public
#include <package.h>
#include <package_data.h>

#define SW_REGISTER_PACKAGE_DRIVER(d) \
    struct __sw ## d { __sw ## d(){ getDrivers().push_back(std::make_unique<d>()); } } ___sw ## d

namespace sw
{

// add more pkg descriptions here

// everything from script file
/**
* Script file may contain:
*   1. package declarations (gather files, deps, build, install)
*   2. configuration(s) settings
*   3. checks
*   4. whatever more script wants
*/
struct SW_BUILDER_API PackageScript
{
    virtual ~PackageScript() = default;

    // void?
    /// run
    virtual bool execute() = 0; // const?

    /// get all packages from script, generic way
    virtual PackageDescriptionMap getPackages() const = 0;

    /// get available configs that is set up in this script
    //void getConfigurations();

    /// generate ide files, other build system scripts, configs etc.
    //void generateBuild();
};

using PackageScriptPtr = std::unique_ptr<PackageScript>;

struct FetchOptions : SourceDownloadOptions
{
    String name_prefix;
    bool apply_version_to_source = false;
};

/**
* Driver loads script file.
*/
struct SW_BUILDER_API Driver
{
    virtual ~Driver() = default;

    virtual FilesOrdered getAvailableFrontends() const = 0;

    /// try to find config in this dir or subdirs
    optional<path> findConfig(const path &dir) const;

    /// read config from dir
    optional<String> readConfig(const path &file_or_dir) const;

    /// only build script file, without loading
    ///
    /// assuming all sources are fetched and script is in the source dir
    virtual PackageScriptPtr build(const path &file_or_dir) const = 0;

    virtual bool buildPackage(const PackageId &pkg) const = 0;

    /// only load script file
    ///
    /// assuming all sources are fetched and script is in the source dir
    virtual PackageScriptPtr load(const path &file_or_dir) const = 0;

    /**
    load script, then fetch all sources to separate subdirs

    General algorithm:
    1. Load script. In script there must be at least one target.
    Do not use conditions on whole script. Rather your users will provide
    conditions for you package.
    2. Fetch all sources.
    3. Load script.
    4. If there are new targets with new sources go to p.2., else stop.
    */
    virtual void fetch(const path &file_or_dir, const FetchOptions &opts = {}, bool parallel = true) const = 0;

    /// load script, fetch all sources using fetch(), then load it again
    ///
    /// source dirs will point to downloaded sources into subdirs
    virtual PackageScriptPtr fetch_and_load(const path &file_or_dir, const FetchOptions &opts = {}, bool parallel = true) const;

    /// full build process, void?
    virtual bool execute(const path &file_or_dir) const;

    //virtual bool build_package(const PackageId &pkg) const = 0;

    // maybe inherit from target with all these properties?
    virtual String getName() const = 0;
    // getDesc()?

    virtual bool run(const PackageId &pkg) const = 0;
};

using DriverPtr = std::unique_ptr<Driver>;
using Drivers = std::vector<DriverPtr>;

// really public? yes, we call it to add (register) more drivers
SW_BUILDER_API
Drivers &getDrivers();

// common routine, maybe add to drivers?
//SW_BUILDER_API
//PackageId extractDriverId(const path &file_or_dir);

} // namespace sw