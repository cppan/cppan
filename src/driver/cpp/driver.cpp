// Copyright (C) 2017-2018 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <sw/driver/cpp/driver.h>

#include <filesystem.h>
#include <package_data.h>
#include <solution.h>

#include <primitives/lock.h>

#include <primitives/log.h>
DECLARE_STATIC_LOGGER(logger, "driver.cpp");

namespace sw::driver::cpp
{

//SW_REGISTER_PACKAGE_DRIVER(CppDriver);

path CppDriver::getConfigFilename() const
{
    return Build::getConfigFilename();
}

optional<path> CppDriver::resolveConfig(const path &file_or_dir) const
{
    auto f = file_or_dir;
    if (fs::is_directory(f))
    {
        if (!hasConfig(f))
            return {};
        f /= getConfigFilename();
    }
    return f;
}

PackageScriptPtr CppDriver::build(const path &file_or_dir) const
{
    auto f = resolveConfig(file_or_dir);
    if (!f)
        return {};
    current_thread_path(f.value().parent_path());

    auto b = std::make_unique<Build>();
    b->Local = true;
    b->configure = true;
    b->build(f.value());
    return b;
}

PackageScriptPtr CppDriver::load(const path &file_or_dir) const
{
    auto f = resolveConfig(file_or_dir);
    if (!f)
        return {};
    current_thread_path(f.value().parent_path());

    auto b = std::make_unique<Build>();
    b->Local = true;
    b->configure = true;
    b->build_and_load(f.value());

    // in order to discover files, deps?
    //b->prepare();

    /*single_process_job(".sw/build", [&f]()
    {
        Build b;
        b.Local = true;
        b.configure = true;
        b.build_and_run(f);

        //LOG_INFO(logger, "Total time: " << t.getTimeFloat());
    });*/

    return b;
}

bool CppDriver::execute(const path &file_or_dir) const
{
    auto f = resolveConfig(file_or_dir);
    if (!f)
        return {};
    current_thread_path(f.value().parent_path());

    if (auto s = load(f.value()); s)
        return s->execute();
    return false;
}

static auto fetch1(const CppDriver *driver, const path &file_or_dir, bool parallel)
{
    auto f = file_or_dir;
    if (fs::is_directory(f))
    {
        if (!driver->hasConfig(f))
            throw std::runtime_error("no config found");
        f /= driver->getConfigFilename();
    }

    auto d = file_or_dir;
    if (!fs::is_directory(d))
        d = d.parent_path();
    d = d / ".sw" / "src";

    Solution::SourceDirMapBySource srcs_old;
    if (parallel)
    {
        bool pp = true; // postpone once!
        while (1)
        {
            auto b = std::make_unique<Build>();
            b->perform_checks = false;
            b->PostponeFileResolving = pp;
            b->source_dirs_by_source = srcs_old;
            if (!pp)
                b->fetch_dir = d;
            b->build_and_load(f);

            Solution::SourceDirMapBySource srcs;
            for (const auto &[pkg, t] : b->solutions.begin()->getChildren())
            {
                auto s = t->source; // make a copy!
                checkSourceAndVersion(s, pkg.getVersion());
                srcs[s] = d / get_source_hash(s);
            }

            // src_old has correct root dirs
            if (srcs.size() == srcs_old.size())
            {
                // reset
                b->fetch_dir.clear();
                for (auto &s : b->solutions)
                    s.fetch_dir.clear();

                return std::tuple{ std::move(b), srcs_old };
            }

            // with this, we only have two iterations
            // This is a limitation, but on the other hand handling of this
            // become too complex for now.
            // For other cases uses non-parallel mode.
            pp = false;

            auto &e = getExecutor();
            Futures<void> fs;
            for (auto &src : srcs)
            {
                fs.push_back(e.push([src = src.first, &d = src.second]
                    {
                        if (!fs::exists(d))
                        {
                            LOG_INFO(logger, "Downloading source:\n" << print_source(src));
                            fs::create_directories(d);
                            ScopedCurrentPath scp(d, CurrentPathScope::Thread);
                            download(src);
                        }
                        d = d / findRootDirectory(d); // pass found regex or files for better root dir lookup
                    }));
            }
            waitAndGet(fs);

            srcs_old = srcs;
        }
    }
    else
    {
        auto b = std::make_unique<Build>();
        b->perform_checks = false;
        b->fetch_dir = d;
        b->build_and_load(f);

        // reset
        b->fetch_dir.clear();
        for (auto &s : b->solutions)
            s.fetch_dir.clear();

        return std::tuple{ std::move(b), srcs_old };
    }
}

void CppDriver::fetch(const path &file_or_dir, bool parallel) const
{
    fetch1(this, file_or_dir, parallel);
}

PackageScriptPtr CppDriver::fetch_and_load(const path &file_or_dir, bool parallel) const
{
    auto [b, srcs] = fetch1(this, file_or_dir, parallel);

    // do not use b->prepare(); !
    // prepare only packages in solution
    auto &e = getExecutor();
    Futures<void> fs;
    for (const auto &[pkg, t] : b->solutions.begin()->getChildren())
    {
        fs.push_back(e.push([t, &srcs, &pkg, parallel]
        {
            if (parallel)
            {
                auto s2 = t->source;
                applyVersionToUrl(s2, pkg.version);
                auto i = srcs.find(s2);
                path rd = i->second / t->RootDirectory;
                t->SourceDir = rd;
            }
            t->prepare();
        }));
    }
    waitAndGet(fs);

    return std::move(b);
}

bool CppDriver::buildPackage(const PackageId &pkg) const
{
    auto b = std::make_unique<Build>();
    b->build_package(pkg.toString());
    return true;
}

bool CppDriver::run(const PackageId &pkg) const
{
    auto b = std::make_unique<Build>();
    b->run_package(pkg.toString());
    return true;
}

} // namespace sw::driver
