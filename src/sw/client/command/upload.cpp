/*
 * SW - Build System and Package Manager
 * Copyright (C) 2017-2019 Egor Pugin
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "commands.h"
#include "../build.h"

#include <sw/driver/build.h>
#include <sw/manager/settings.h>
#include <sw/manager/api.h>

#include <nlohmann/json.hpp>
#include <primitives/pack.h>

#include <primitives/log.h>
DECLARE_STATIC_LOGGER(logger, "upload");

extern ::cl::opt<String> build_arg_update;

static ::cl::opt<String> upload_remote(::cl::Positional, ::cl::desc("Remote name"), ::cl::sub(subcommand_upload));
String gUploadPrefix;
static ::cl::opt<String, true> upload_prefix(::cl::Positional, ::cl::desc("Prefix path"), ::cl::sub(subcommand_upload),
    ::cl::Required, ::cl::location(gUploadPrefix));

sw::Remote *find_remote(sw::Settings &s, const String &name);

SUBCOMMAND_DECL(upload)
{
    auto swctx = createSwContext();
    cli_upload(*swctx);
}

sw::PackageDescriptionMap getPackages(const sw::SwContext &swctx, const sw::SourceDirMap &sources)
{
    using namespace sw;

    PackageDescriptionMap m;
    for (auto &[pkg, td] : swctx.getTargets())
    {
        // deps
        if (pkg.ppath.isAbsolute())
            continue;
        auto t = td.getAnyTarget();
        if (!t->isReal())
            continue;

        nlohmann::json j;

        // source, version, path
        t->getSource().save(j["source"]);
        j["version"] = pkg.getVersion().toString();
        j["path"] = pkg.ppath.toString();

        // find root dir
        path rd;
        if (!sources.empty())
        {
            auto src = t->getSource().clone(); // copy
            src->applyVersion(pkg.version);
            auto si = sources.find(src->getHash());
            if (si == sources.end())
                throw SW_RUNTIME_ERROR("no such source");
            rd = si->second;
        }
        j["root_dir"] = normalize_path(rd);

        // double check files (normalize them)
        Files files;
        for (auto &f : t->getSourceFiles())
            files.insert(f.lexically_normal());

        // we put files under SW_SDIR_NAME to keep space near it
        // e.g. for patch dir or other dirs (server provided files)
        // we might unpack to other dir, but server could push service files in neighbor dirs like gpg keys etc
        nlohmann::json jm;
        auto files_map1 = primitives::pack::prepare_files(files, rd.lexically_normal());
        for (const auto &[f1, f2] : files_map1)
        {
            nlohmann::json jf;
            jf["from"] = normalize_path(f1);
            jf["to"] = normalize_path(f2);
            j["files"].push_back(jf);
        }

        // deps
        for (auto &d : t->getDependencies())
        {
            // filter out predefined targets
            if (swctx.getPredefinedTargets().find(d->getUnresolvedPackage().ppath) != swctx.getPredefinedTargets().end(d->getUnresolvedPackage().ppath))
                continue;

            nlohmann::json jd;
            jd["path"] = d->getUnresolvedPackage().ppath.toString();
            jd["range"] = d->getUnresolvedPackage().range.toString();
            j["dependencies"].push_back(jd);
        }

        auto s = j.dump();
        m[pkg] = std::make_unique<JsonPackageDescription>(s);
    }
    return m;
}

SUBCOMMAND_DECL2(upload)
{
    auto sources = fetch(swctx);
    if (sources.empty())
        throw SW_RUNTIME_ERROR("Empty target sources");

    auto m = getPackages(swctx, sources);

    // dbg purposes
    for (auto &[id, d] : m)
    {
        write_file(fs::current_path() / SW_BINARY_DIR / "upload" / id.toString() += ".json", d->getString());
        auto id2 = id;
        id2.ppath = sw::PackagePath(upload_prefix) / id2.ppath;
        LOG_INFO(logger, "Uploading " + id2.toString());
    }

    // select remote first
    auto &us = sw::Settings::get_user_settings();
    auto current_remote = &*us.remotes.begin();
    if (!upload_remote.empty())
        current_remote = find_remote(us, upload_remote);

    // send signatures (gpg)
    // -k KEY1 -k KEY2
    auto api = current_remote->getApi();
    api->addVersion(gUploadPrefix, m, swctx.getSpecification());
}
