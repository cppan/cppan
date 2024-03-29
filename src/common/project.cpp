/*
 * Copyright (C) 2016-2017, Egor Pugin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "project.h"

#include "bazel/bazel.h"
#include "checks_detail.h"
#include "config.h"
#include "http.h"
#include "resolver.h"

#include "printers/printer.h"

#include <exceptions.h>

#include <boost/algorithm/string.hpp>

#include <primitives/command.h>
#include <primitives/hash.h>
#include <primitives/pack.h>
#include <primitives/patch.h>

#include <regex>

#include <primitives/log.h>
DECLARE_STATIC_LOGGER(logger, "project");

using MimeType = String;
using MimeTypes = std::set<MimeType>;

static const std::vector<String> additional_include_dirs_public{
    "${BDIR}",
};

static const std::vector<String> additional_include_dirs_private{
    "${BDIR_PRIVATE}",
};

const MimeTypes source_mime_types{
    "application/xml",
    "text/xml",

    "inode/x-empty", // empty file

    "text/x-asm",
    "text/x-c",
    "text/x-c++",
    "text/plain",
    "text/html", // ?
    "text/tex", // ? file with many comments can be this
    "text/x-makefile", // some .in files
    "text/x-shellscript", // some .in files
};

const std::set<String> header_file_extensions{
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".h++",
    ".H++",
    ".HPP",
    ".H",
};

const std::set<String> source_file_extensions{
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".c++",
    ".C++",
    ".CPP",
    // Objective-C
    ".m",
    ".mm",
    ".C",
};

const std::set<String> other_source_file_extensions{
    ".s",
    ".S",
    ".asm",
    ".ipp",
    ".inl",
};

auto escape_regex_symbols(const String &s)
{
    return boost::replace_all_copy(s, "+", "\\+");
}

bool is_allowed_file_extension(const path &p)
{
    auto e = p.extension().string();
    auto check = [&e](const auto &src)
    {
        return std::any_of(src.begin(), src.end(), [&e](const auto &s)
        {
            return s == e;
        });
    };
    if (check(header_file_extensions) ||
        check(source_file_extensions) ||
        check(other_source_file_extensions))
        return true;
    return false;
}

bool is_valid_file_type(const MimeTypes &types, const path &p, const String &s, String *error = nullptr, bool check_ext = false)
{
    auto mime = s.substr(0, s.find(';'));
    bool ok = std::any_of(types.begin(), types.end(), [&mime](const auto &s)
    {
        return mime == s;
    });
    if (!ok && check_ext)
        ok = is_allowed_file_extension(p);
    if (!ok && error)
        *error = "not supported: " + p.string() + ", mime: " + mime;
    return ok;
}

bool is_valid_file_type(const MimeTypes &types, const path &p, String *error = nullptr, bool check_ext = false)
{
    primitives::Command c;
    c.setProgram("file");
    c.arguments.push_back("-ib");
    c.arguments.push_back(p.string());
    c.execute();
    return is_valid_file_type(types, p, c.out.text, error, check_ext);
}

bool is_valid_source_mime_type(const path &p, String *error = nullptr)
{
    return is_valid_file_type(source_mime_types, p, error, true);
}

bool is_valid_source(const path &p)
{
    if (!p.has_extension())
        return false;
    auto e = p.extension().string();
    return source_file_extensions.find(e) != source_file_extensions.end();
}

bool check_filename(const String &s)
{
    for (auto &c : s)
    {
        if (c < 0 || c > 127)
            return false;
        if (isalnum((uint8_t)c))
            continue;
        switch (c)
        {
        case '/':
        case '\\':
        case ':':
        case '.':
        case '_':
        case '-':
        case '+':
            break;
        default:
            return false;
        }
    }
    return true;
}

void check_file_types(const Files &files)
{
    if (files.empty())
        return;

    String errors;
    for (auto &file : files)
    {
        auto s = file.string();
        if (!check_filename(s))
            errors += "File '" + s + "' has prohibited symbols\n";
    }
    if (!errors.empty())
        throw std::runtime_error("Project sources did not pass file checks:\n" + errors);

    auto fn = get_temp_filename();
    std::ofstream o(fn, std::ios::binary | std::ios::out);
    if (!o)
        throw std::runtime_error("Cannot open file for writing: " + to_printable_string(fn));
    for (auto &file : files)
        o << "file -ib " << normalize_path(file) << "\n";
    o.close();

    primitives::Command c;
    c.setProgram("sh");
    c.arguments.push_back(fn.string());
    std::error_code ec;
    c.execute(ec);
    fs::remove(fn);

    if (ec)
        throw std::runtime_error("Error during file checking: rc = " + std::to_string(c.exit_code.value()));

    std::vector<String> lines, sh_out;
    boost::split(sh_out, c.out.text, boost::is_any_of("\r\n"));
    for (auto &s : sh_out)
    {
        boost::trim(s);
        if (!s.empty())
            lines.push_back(s);
    }
    if (lines.size() != files.size())
        throw std::runtime_error("Error during file checking: number of output lines does not match");

    int i = 0;
    for (auto &file : files)
    {
        String error;
        is_valid_file_type(source_mime_types, file, lines[i], &error, true);
        if (!error.empty())
            errors += error + "\n";
        i++;
    }
    if (!errors.empty())
        throw std::runtime_error("Project did not pass file checks:\n" + errors);
}

void get_config_insertion(const yaml &n, const String &key, String &dst)
{
    dst = get_scalar<String>(n, key);
    boost::trim(dst);
}

void load_source_and_version(const yaml &root, Source &source, Version &version)
{
    String ver;
    YAML_EXTRACT_VAR(root, ver, "version", String);
    if (!ver.empty())
        version = Version(ver);
    if (!load_source(root, source))
        return;
    visit([&version](auto &v) { v.loadVersion(version); }, source);
}

void BuildSystemConfigInsertions::load(const yaml &n)
{
#define BSI(x) get_config_insertion(n, #x, x);
#include "bsi.inl"
#undef BSI
}

void BuildSystemConfigInsertions::save(yaml &n) const
{
#define BSI(x) if (!x.empty()) n[#x] = x;
#include "bsi.inl"
#undef BSI
}

void BuildSystemConfigInsertions::merge(yaml &dst, const yaml &src)
{
#define BSI(x)                                                              \
    if (src[#x].IsDefined())                                                \
    {                                                                       \
        if (dst[#x].IsDefined())                                            \
            dst[#x] = src[#x].as<String>() + "\n\n" + dst[#x].as<String>(); \
        else                                                                \
            dst[#x] = src[#x].as<String>();                                 \
    }
#include "bsi.inl"
#undef BSI
}

void BuildSystemConfigInsertions::merge_and_remove(yaml &dst, yaml &src)
{
    merge(dst, src);
    remove(src);
}

void BuildSystemConfigInsertions::remove(yaml &src)
{
#define BSI(x) src.remove(#x);
#include "bsi.inl"
#undef BSI
}

Strings BuildSystemConfigInsertions::getStrings()
{
    static Strings strings
    {
#define BSI(x) #x,
#include "bsi.inl"
#undef BSI
    };
    return strings;
}

#define UNIDIFF_PREFIX "unidiff_patch_"

void Patch::load(const yaml &root)
{
    auto load_replace = [this, &root](auto &a, const String &k)
    {
        get_map_and_iterate(root, k, [this, &a](auto &v)
        {
            auto k = v.first.template as<String>();
            if (v.second.IsScalar())
            {
                auto vv = v.second.template as<String>();
                if (k.find(UNIDIFF_PREFIX) != 0)
                    a.emplace_back(k, vv);
                else
                    file_patches.emplace_back(k.substr(strlen(UNIDIFF_PREFIX)), vv);
            }
            else if (v.second.IsMap())
            {
                if (!(v.second["from"].IsDefined() && v.second["to"].IsDefined()))
                    throw std::runtime_error("There are no 'from' and 'to' inside '" + k + "'");
                auto from = v.second["from"].template as<String>();
                auto to = v.second["to"].template as<String>();
                if (from.find(UNIDIFF_PREFIX) != 0)
                    a.emplace_back(from, to);
                else
                    file_patches.emplace_back(from.substr(strlen(UNIDIFF_PREFIX)), to);
            }
            else
                throw std::runtime_error("Members of '" + k + "' must be scalars or maps");
        });
    };
    load_replace(replace, "replace");
    load_replace(regex_replace, "regex_replace");

    // yaml add one more newline there
    for (auto &[s, f] : file_patches)
    {
        if (!f.empty())
            f.resize(f.size() - 1);
    }
}

void Patch::save(yaml &node) const
{
    auto save_replace = [&node](const auto &a, const auto &k, const String &prefix = {})
    {
        if (a.empty())
            return;
        yaml root;
        for (auto &r : a)
            root[prefix + r.first] = r.second;
        node["patch"][k] = root;
    };
    save_replace(replace, "replace");
    save_replace(regex_replace, "regex_replace");
    save_replace(file_patches, "replace", UNIDIFF_PREFIX);
}

void Patch::patchSources(const Project &prj, const FilesSorted &files) const
{
    auto rd = prj.pkg.getDirSrc();
    for (auto &[s, f] : file_patches)
    {
        path p = s;
        if (p.is_absolute())
        {
            LOG_ERROR(logger, "patch path file is absolute: " << s);
            continue;
        }

        auto fn = rd / p;
        auto t = read_file(fn);

        auto fn_patch = fn;
        fn_patch += ".orig." + sha1(f).substr(0, 8);

        if (fs::exists(fn_patch))
            continue;

        auto r = primitives::patch::patch(t, f);
        if (!r.first)
        {
            LOG_ERROR(logger, "cannot apply patch to: " << s << ": " << r.second);
            continue;
        }

        write_file(fn, r.second);
        write_file(fn_patch, t); // save orig
    }

    if (replace.empty() && regex_replace.empty())
        return;
    std::vector<std::pair<std::regex, String>> regex_prepared;
    for (auto &p : regex_replace)
        regex_prepared.emplace_back(std::regex(p.first), p.second);
    for (auto &f : files)
    {
        auto s = read_file(f);
        for (auto &p : replace)
            boost::algorithm::replace_all(s, p.first, p.second);
        for (auto &p : regex_prepared)
            s = std::regex_replace(s, p.first, p.second);
        write_file_if_different(f, s);
    }
}

Project::Project()
    : Project(ProjectPath())
{
}

Project::Project(const ProjectPath &root_project)
    : root_project(root_project)
{
}

void Project::findSources(path p)
{
    // output file list (files) must contain absolute paths
    //

    // correct root dir is detected and set during load phase
    if (p.empty())
        p = current_thread_path();
    if (p != root_directory)
    {
        if (root_directory.is_absolute())
            p = root_directory;
        else
            p /= root_directory;
    }

    if (import_from_bazel)
    {
        path bfn;
        for (auto &f : bazel_filenames)
        {
            if (fs::exists(p / f))
            {
                bfn = p / f;
                break;
            }
        }

        auto b = read_file(bfn);
        auto f = bazel::parse(b);
        String project_name;
        if (!pkg.ppath.empty())
            project_name = pkg.ppath.back();
        auto files1 = f.getFiles(bazel_target_name.empty() ? project_name : bazel_target_name, bazel_target_function);
        std::vector<std::string> files(files1.begin(), files1.end());

        // prepare bazel filenames:
        // - remove quotes
        // - escape non-regex symbols
        for (auto &f : files)
        {
            boost::replace_all(f, "\"", "");
            boost::replace_all(f, "+", "\\+");
        }

        sources.insert(files.begin(), files.end());
        sources.insert(bfn.filename().string());
    }

    error_code ec;
    for (auto i = sources.begin(); i != sources.end();)
    {
        auto f = p / *i;
        if (fs::exists(f, ec) && fs::is_regular_file(f))
        {
            files.insert(f);
            sources.erase(i++);
            continue;
        }
        ++i;
    }

    if ((sources.empty() && files.empty()) && !empty)
        throw std::runtime_error("'files' must be populated");

    auto create_regex = [&p](const auto &e)
    {
        auto s = to_printable_string(normalize_path(p));
        s = escape_regex_symbols(s);
        if (!s.empty() && s.back() != '/')
            s += "/";
        return std::regex(s + e);
    };

    std::unordered_map<String, std::regex> rgxs, rgxs_exclude;

    for (auto &e : sources)
        rgxs[e] = create_regex(e);
    if (!rgxs.empty())
    {
        for (auto &f : fs::recursive_directory_iterator(p))
        {
            if (!fs::is_regular_file(f))
                continue;

            auto s = to_printable_string(normalize_path(f));
            for (auto &e : rgxs)
            {
                if (!std::regex_match(s, e.second))
                    continue;
                files.insert(f);
                break;
            }
        }
    }

    for (auto &e : exclude_from_package)
        rgxs_exclude[e] = create_regex(e);
    if (!rgxs_exclude.empty())
    {
        auto to_remove = files;
        for (auto &f : files)
        {
            auto s = to_printable_string(normalize_path(f));
            for (auto &e : rgxs_exclude)
            {
                if (!std::regex_match(s, e.second))
                    continue;
                to_remove.erase(f);
                break;
            }
        }
        files = to_remove;
    }

    if (files.empty() && !empty)
        throw std::runtime_error("no files found");

    // disable on windows
#ifndef _WIN32
    // disabled for some time
    //if (!custom)
    //    check_file_types(files);
#endif

    // do not check if forced header_only (no matter true or false)
    if (!header_only && !custom)
        header_only = std::none_of(files.begin(), files.end(), is_valid_source);

    // when we see only headers, mark type as library
    // useful for local projects
    if (header_only && header_only.value())
    {
        type = ProjectType::Library;
        pkg.flags.set(pfHeaderOnly);
    }

    auto check_license = [this](auto &name, String *error = nullptr)
    {
        auto license_error = [&error](auto &err)
        {
            if (error)
            {
                *error = err;
                return false;
            }
            throw std::runtime_error(err);
        };
        if (!fs::exists(root_directory / name))
            return license_error("license does not exists");
        if (fs::file_size(root_directory / name) > 512 * 1024)
            return license_error("license is invalid (should be text/plain and less than 512 KB)");
        return true;
    };

    if (!pkg.flags[pfLocalProject])
    {
        if (!license.empty())
        {
            if (check_license(license))
                files.insert(license);
        }
        else
        {
            String error;
            auto try_license = [&error, &check_license, this](auto &lic)
            {
                if (check_license(lic, &error))
                {
                    files.insert(lic);
                    return true;
                }
                return false;
            };
            if (try_license("LICENSE") ||
                try_license("COPYING") ||
                try_license("Copying.txt") ||
                try_license("LICENSE.txt") ||
                try_license("license.txt") ||
                try_license("LICENSE.md"))
                (void)error;
        }
    }

    if (!root_directory.empty() && !pkg.flags[pfLocalProject] &&
        fs::absolute(CPPAN_FILENAME) != fs::absolute(root_directory / CPPAN_FILENAME))
        fs::copy_file(CPPAN_FILENAME, root_directory / CPPAN_FILENAME, fs::copy_options::overwrite_existing);
    if (fs::exists(p / CPPAN_FILENAME))
        files.insert(p / CPPAN_FILENAME);
    else if (fs::exists(current_thread_path() / CPPAN_FILENAME))
        files.insert(current_thread_path() / CPPAN_FILENAME);
    else
        files.insert(CPPAN_FILENAME);
}

bool Project::writeArchive(const path &fn) const
{
    // some files have not abolute paths (e.g., license files)
    // do not remove until fixed
    ScopedCurrentPath cp(root_directory);
    return pack_files(fn, files, cp.get_cwd());
}

void Project::save_dependencies(yaml &node) const
{
    if (dependencies.empty())
        return;

    for (auto &dd : dependencies)
    {
        auto &d = dd.second;
        yaml c = node[DEPENDENCIES_NODE];
        yaml n;
        if (d.flags[pfPrivateDependency])
            n = c["private"];
        else
            n = c["public"];

        // always save as map
        yaml n2;
        n2["version"] = d.version.toAnyVersion();

        if (!d.reference.empty())
            n2["reference"] = d.reference;
        for (auto &c : d.conditions)
            n2["conditions"].push_back(c);
        if (d.flags[pfIncludeDirectoriesOnly])
            n2[INCLUDE_DIRECTORIES_ONLY] = true;

        n[dd.first] = n2;
    }
}

ProjectPath Project::relative_name_to_absolute(const String &name)
{
    ProjectPath ppath;
    if (name.empty())
    {
        if (subdir.empty())
            return ppath;
        else
            return ppath = subdir;
    }
    if (ProjectPath(name).is_relative())
    {
        if (allow_relative_project_names)
        {
            //ppath.push_back(name);
            if (subdir.empty())
                ppath = name;
            else
                ppath = subdir + "." + name;
            return ppath;
        }
        if (root_project.empty())
            throw std::runtime_error("You're using relative names, but 'root_project' is missing");
        // we split entered 'name' because it may contain dots also
        if (subdir.empty())
            ppath = root_project / ProjectPath(name);
        else
            // we do not split subdirs yet
            ppath = root_project / subdir / ProjectPath(name);
    }
    else
        ppath = name;
    return ppath;
}

void Project::load(const yaml &root)
{
    load_source_and_version(root, source, pkg.version);

    YAML_EXTRACT_AUTO(empty);
    YAML_EXTRACT_AUTO(custom);

    YAML_EXTRACT_AUTO(shared_only);
    YAML_EXTRACT_AUTO(static_only);
    YAML_EXTRACT_VAR(root, header_only, "header_only", bool);

    if (shared_only && static_only)
        throw std::runtime_error("Project cannot be static and shared simultaneously");

    YAML_EXTRACT_AUTO(import_from_bazel);
    YAML_EXTRACT_AUTO(bazel_target_name);
    YAML_EXTRACT_AUTO(bazel_target_function);

    YAML_EXTRACT_AUTO(prefer_binaries);
    YAML_EXTRACT_AUTO(export_all_symbols);
    YAML_EXTRACT_AUTO(export_if_static);
    YAML_EXTRACT_AUTO(rc_enabled);
    //YAML_EXTRACT_AUTO(disabled);
    YAML_EXTRACT_AUTO(skip_on_server);
    YAML_EXTRACT_AUTO(create_default_api);
    YAML_EXTRACT_AUTO(default_api_start);
    YAML_EXTRACT_AUTO(build_dependencies_with_same_config);
    YAML_EXTRACT_AUTO(copy_to_output_dir);

    api_name = get_sequence_set<String>(root, "api_name");

    YAML_EXTRACT_AUTO(output_name);
    YAML_EXTRACT_AUTO(condition);
    YAML_EXTRACT_AUTO(include_script);

    // standards
    {
        YAML_EXTRACT_AUTO(c_standard);
        if (c_standard == 0)
        {
            YAML_EXTRACT_VAR(root, c_standard, "c", int);
        }
        YAML_EXTRACT_AUTO(c_extensions);

        String cxx;
        YAML_EXTRACT_VAR(root, cxx, "cxx_standard", String);
        if (cxx.empty())
            YAML_EXTRACT_VAR(root, cxx, "c++", String);
        YAML_EXTRACT_AUTO(cxx_extensions);

        if (!cxx.empty())
        {
            try
            {
                cxx_standard = std::stoi(cxx);
            }
            catch (const std::exception&)
            {
                if (cxx == "1z")
                    cxx_standard = 17;
                else if (cxx == "2x")
                    cxx_standard = 20;
            }
        }
    }

    license = get_scalar<String>(root, "license");

    auto read_dir = [&root](auto &p, const String &s)
    {
        get_scalar_f(root, s, [&p, &s](const auto &n)
        {
            auto cp = current_thread_path();
            p = n.template as<String>();
            if (!is_under_root_by_prefix_path(cp / p, cp))
                throw std::runtime_error("'" + s + "' must not point outside the current dir: " + p.string() + ", " + cp.string());
        });
    };

    read_dir(root_directory, "root_directory");
    if (root_directory.empty())
        read_dir(root_directory, "root_dir");

    read_dir(unpack_directory, "unpack_directory");
    if (unpack_directory.empty())
        read_dir(unpack_directory, "unpack_dir");

    YAML_EXTRACT_AUTO(output_directory);
    if (output_directory.empty())
        YAML_EXTRACT_VAR(root, output_directory, "output_dir", String);

    // include_directories
    {
        get_variety(root, "include_directories",
            [this](const auto &d)
        {
            include_directories.public_.insert(d.template as<String>());
        },
            [this](const auto &dall)
        {
            for (auto d : dall)
                include_directories.public_.insert(d.template as<String>());
        },
            [this, &root](const auto &)
        {
            get_map_and_iterate(root, "include_directories", [this](const auto &n)
            {
                auto f = n.first.template as<String>();
                auto s = get_sequence<String>(n.second);
                if (f == "public")
                    include_directories.public_.insert(s.begin(), s.end());
                else if (f == "private")
                    include_directories.private_.insert(s.begin(), s.end());
                else if (f == "interface")
                    include_directories.interface_.insert(s.begin(), s.end());
                else
                    throw std::runtime_error("include key must be only 'public' or 'private' or 'interface'");
            });
        });
    }

    bs_insertions.load(root);
    options = loadOptionsMap(root);

    // deps
    {
        auto read_version = [](auto &dependency, const String &v)
        {
            if (!dependency.flags[pfLocalProject])
            {
                dependency.version = v;
                return;
            }

            if (rd.has_local_package(dependency.ppath))
                dependency.version = Version(LOCAL_VERSION_NAME);
            else
            {
                auto nppath = dependency.ppath / v;
                if (rd.has_local_package(nppath))
                {
                    dependency.ppath = nppath;
                    dependency.version = Version(LOCAL_VERSION_NAME);
                }
                else
                    throw std::runtime_error("Unknown local dependency: " + nppath.toString());
            }
        };

        auto read_single_dep = [this, &read_version](const auto &d, Package dependency = Package())
        {
            bool local_ok = false;
            if (d.IsScalar())
            {
                auto p = extractFromStringAny(d.template as<String>());
                dependency.ppath = this->relative_name_to_absolute(p.ppath.toString());
                dependency.version = p.version;
            }
            else if (d.IsMap())
            {
                // read only field related to ppath - name, local
                if (d["name"].IsDefined())
                    dependency.ppath = this->relative_name_to_absolute(d["name"].template as<String>());
                if (d["package"].IsDefined())
                    dependency.ppath = this->relative_name_to_absolute(d["package"].template as<String>());
                if (dependency.ppath.empty() && d.size() == 1)
                {
                    dependency.ppath = this->relative_name_to_absolute(d.begin()->first.template as<String>());
                    if (dependency.ppath.is_loc())
                        dependency.flags.set(pfLocalProject);
                    read_version(dependency, d.begin()->second.template as<String>());
                }
                if (d["local"].IsDefined() && allow_local_dependencies)
                {
                    auto p = d["local"].template as<String>();
                    Package pkg;
                    pkg.ppath = p;
                    if (!d["version"].IsDefined())
                        throw std::runtime_error("dependency: local is present, but version is not: " + p);
                    if (rd.known_local_packages.find(pkg) != rd.known_local_packages.end())
                        local_ok = true;
                    pkg.version = d["version"].template as<String>();
                    if (rd.known_local_packages.find(pkg) != rd.known_local_packages.end())
                        local_ok = true;
                    if (local_ok)
                        dependency.ppath = p;
                }
            }

            if (dependency.ppath.is_loc())
            {
                dependency.flags.set(pfLocalProject);

                // version will be read for local project
                // even 2nd arg is not valid
                String v;
                if (d.IsMap() && d["version"].IsDefined())
                    v = d["version"].template as<String>();
                read_version(dependency, v);
            }

            if (d.IsMap())
            {
                // read other map fields
                if (d["version"].IsDefined())
                {
                    read_version(dependency, d["version"].template as<String>());
                    if (local_ok)
                        dependency.version = Version(LOCAL_VERSION_NAME);
                }
                if (d["ref"].IsDefined())
                    dependency.reference = d["ref"].template as<String>();
                if (d["reference"].IsDefined())
                    dependency.reference = d["reference"].template as<String>();
                if (d[INCLUDE_DIRECTORIES_ONLY].IsDefined())
                    dependency.flags.set(pfIncludeDirectoriesOnly, d[INCLUDE_DIRECTORIES_ONLY].template as<bool>());

                // conditions
                dependency.conditions = get_sequence_set<String>(d, "condition");
                auto conds = get_sequence_set<String>(d, "conditions");
                dependency.conditions.insert(conds.begin(), conds.end());
            }

            if (dependency.flags[pfLocalProject])
                dependency.createNames();

            return dependency;
        };

        auto get_deps = [&](const auto &node)
        {
            get_variety(root, node,
                [this, &read_single_dep](const auto &d)
            {
                auto dep = read_single_dep(d);
                dependencies[dep.ppath.toString()] = dep;
            },
                [this, &read_single_dep](const auto &dall)
            {
                for (auto d : dall)
                {
                    auto dep = read_single_dep(d);
                    dependencies[dep.ppath.toString()] = dep;
                }
            },
                [this, &read_single_dep, &read_version](const auto &dall)
            {
                auto get_dep = [this, &read_version, &read_single_dep](const auto &d)
                {
                    Package dependency;

                    dependency.ppath = this->relative_name_to_absolute(d.first.template as<String>());
                    if (dependency.ppath.is_loc())
                        dependency.flags.set(pfLocalProject);

                    if (d.second.IsScalar())
                        read_version(dependency, d.second.template as<String>());
                    else if (d.second.IsMap())
                        return read_single_dep(d.second, dependency);
                    else
                        throw std::runtime_error("Dependency should be a scalar or a map");

                    if (dependency.flags[pfLocalProject])
                        dependency.createNames();

                    return dependency;
                };

                auto extract_deps = [&get_dep, &read_single_dep](const auto &dall, const auto &str)
                {
                    Packages deps;
                    auto priv = dall[str];
                    if (!priv.IsDefined())
                        return deps;
                    if (priv.IsMap())
                    {
                        get_map_and_iterate(dall, str,
                            [&get_dep, &deps](const auto &d)
                        {
                            auto dep = get_dep(d);
                            deps[dep.ppath.toString()] = dep;
                        });
                    }
                    else if (priv.IsSequence())
                    {
                        for (auto d : priv)
                        {
                            auto dep = read_single_dep(d);
                            deps[dep.ppath.toString()] = dep;
                        }
                    }
                    return deps;
                };

                auto extract_deps_from_node = [&extract_deps, &get_dep](const auto &node)
                {
                    auto deps_private = extract_deps(node, "private");
                    auto deps = extract_deps(node, "public");

                    for (auto &d : deps_private)
                    {
                        d.second.flags.set(pfPrivateDependency);
                        deps.insert(d);
                    }

                    if (deps.empty() && deps_private.empty())
                    {
                        for (auto d : node)
                        {
                            auto dep = get_dep(d);
                            deps[dep.ppath.toString()] = dep;
                        }
                    }

                    return deps;
                };

                auto ed = extract_deps_from_node(dall);
                dependencies.insert(ed.begin(), ed.end());

                // conditional deps
                /*for (auto n : dall)
                {
                    auto spec = n.first.as<String>();
                    if (spec == "private" || spec == "public")
                        continue;
                    if (n.second.IsSequence())
                    {
                        for (auto d : n.second)
                        {
                            auto dep = read_single_dep(d);
                            dep.condition = spec;
                            dependencies[dep.ppath.toString()] = dep;
                        }
                    }
                    else if (n.second.IsMap())
                    {
                        ed = extract_deps_from_node(n.second, spec);
                        dependencies.insert(ed.begin(), ed.end());
                    }
                }

                if (deps.empty() && deps_private.empty())
                {
                    for (auto d : node)
                    {
                        auto dep = get_dep(d);
                        deps[dep.ppath.toString()] = dep;
                    }
                }*/
            });
        };

        get_deps(DEPENDENCIES_NODE);
        get_deps("deps");
    }

    auto read_sources = [&root](auto &a, const String &key, bool required = true)
    {
        a.clear();
        auto files = root[key];
        if (!files.IsDefined())
            return;
        if (files.IsScalar())
        {
            a.insert(files.as<String>());
        }
        else if (files.IsSequence())
        {
            for (const auto &v : files)
                a.insert(v.as<String>());
        }
        else if (files.IsMap())
        {
            for (const auto &group : files)
            {
                if (group.second.IsScalar())
                    a.insert(group.second.as<String>());
                else if (group.second.IsSequence())
                {
                    for (const auto &v : group.second)
                        a.insert(v.as<String>());
                }
                else if (group.second.IsMap())
                {
                    String root = get_scalar<String>(group.second, "root");
                    auto v = get_sequence<String>(group.second, "files");
                    for (auto &e : v)
                        a.insert(root + "/" + e);
                }
            }
        }
    };

    read_sources(sources, "files");
    read_sources(build_files, "build");
    read_sources(exclude_from_package, "exclude_from_package");
    read_sources(exclude_from_build, "exclude_from_build");
    read_sources(public_headers, "public_headers");
    include_hints = get_sequence_set<String>(root, "include_hints");

    aliases = get_sequence_set<String>(root, "aliases");

    checks.load(root);
    checks_prefixes = get_sequence_set<String>(root, "checks_prefixes");
    if (checks_prefixes.empty())
        checks_prefixes = get_sequence_set<String>(root, "checks_prefix");

    const auto &patch_node = root["patch"];
    if (patch_node.IsDefined())
        patch.load(patch_node);

    YAML_EXTRACT_AUTO(name);

    String pt;
    YAML_EXTRACT_VAR(root, pt, "type", String);
    if (pt == "l" || pt == "lib" || pt == "library")
        type = ProjectType::Library;
    else if (pt == "e" || pt == "exe" || pt == "executable")
        type = ProjectType::Executable;

    String lt;
    YAML_EXTRACT_VAR(root, lt, "library_type", String);
    if (lt == "static")
    {
        library_type = LibraryType::Static;
        static_only = true;
    }
    if (lt == "shared" || lt == "dll")
    {
        library_type = LibraryType::Shared;
        shared_only = true;
    }
    if (lt == "module")
    {
        library_type = LibraryType::Module;
        shared_only = true;
    }

    String et;
    YAML_EXTRACT_VAR(root, et, "executable_type", String);
    if (et == "win32")
        executable_type = ExecutableType::Win32;

    // after loading process input data where it's necessary

    // we also store original data in ptr
    // this is useful for printing original config (project)
    original_project = std::make_shared<Project>(*this);

    // we're trying to find root directory
    // to make some following default checks available
    // try to detect and prepend root dir
    {
        auto root = current_thread_path();
        if (is_local)
            root = findRootDirectory(root);
        if (root_directory.empty() || !fs::exists(root / root_directory))
            root_directory = root;
        else if (root_directory != root)
            root_directory = root / root_directory;
    }

    static const auto source_dir_names = { "src", "source", "sources", "lib", "library" };

    // idirs
    {
        bool iempty = include_directories.empty();
        if (defaults_allowed && iempty)
        {
            /*if (fs::exists("include"))
                include_directories.public_.insert("include");
            else
            {*/
            // root_directory part must be checked on server side or during local pkg build
            // second part - on installed package
            if (fs::exists(root_directory / "include"))
                include_directories.public_.insert("include");
            else if (fs::exists(root_directory / "includes"))
                include_directories.public_.insert("includes");
            else
            {
                include_directories.public_.insert(".");
                // one case left: root_directory / "."
            }
            //}
        }
        if (defaults_allowed && iempty)
        {
            std::function<void(const Strings &)> autodetect_source_dir;
            autodetect_source_dir = [this, &autodetect_source_dir](const Strings &dirs)
            {
                const auto &current = dirs[0];
                const auto &next = dirs[1];
                /*if (fs::exists(current))
                {
                    if (fs::exists("include"))
                        include_directories.private_.insert(current);
                    else
                        include_directories.public_.insert(current);
                }
                else
                {*/
                if (fs::exists(root_directory / current))
                {
                    if (fs::exists(root_directory / "include"))
                        include_directories.private_.insert(current);
                    else if (fs::exists(root_directory / "includes"))
                        include_directories.private_.insert(current);
                    else
                    {
                        include_directories.public_.insert(current);
                        // one case left: root_directory / "src"
                    }
                }
                else
                {
                    // now check next dir
                    if (!next.empty())
                        autodetect_source_dir({ dirs.begin() + 1, dirs.end() });
                }
                //}
            };
            static Strings dirs(source_dir_names.begin(), source_dir_names.end());
            // keep the empty entry at the end for autodetect_source_dir()
            if (dirs.back() != "")
                dirs.push_back("");
            autodetect_source_dir(dirs);
        }
        include_directories.public_.insert(additional_include_dirs_public.begin(), additional_include_dirs_public.end());
        include_directories.private_.insert(additional_include_dirs_private.begin(), additional_include_dirs_private.end());
    }

    // files
    files_loaded = root["files"].IsDefined() && !sources.empty();
    if (defaults_allowed && sources.empty() && !import_from_bazel)
    {
        // try to add some default dirs
        // root_directory will be removed (entered),
        // so do not insert like 'insert(root_directory / "dir/.*");'
        if (fs::exists(root_directory / "include"))
            sources.insert("include/.*");
        else if (fs::exists(root_directory / "includes"))
            sources.insert("includes/.*");
        for (auto &d : source_dir_names)
        {
            if (fs::exists(root_directory / d))
                sources.insert(d + "/.*"s);
        }

        if (sources.empty())
        {
            // no include, source dirs
            // try to add all types of C/C++ program files to gather
            // regex means all sources in root dir (without slashes '/')
            for (auto &v : header_file_extensions)
                sources.insert("[^/]*\\" + escape_regex_symbols(v));
            for (auto &v : source_file_extensions)
                sources.insert("[^/]*\\" + escape_regex_symbols(v));
        }
    }
    if (import_from_bazel)
    {
        for (auto &bfn : bazel_filenames)
            exclude_from_build.insert(bfn);
    }
}

yaml Project::save() const
{
    if (original_project)
        return original_project->save();

    yaml root;

#define ADD_IF_VAL(x, c, v) if (c) root[#x] = v
#define ADD_IF_NOT_VAL(x, c, v) if (!c) root[#x] = v
#define ADD_IF_VAL_TRIPLE(x) ADD_IF_VAL(x, x, x)
#define ADD_IF_NOT_VAL_TRIPLE(x) ADD_IF_NOT_VAL(x, x, x)
#define ADD_IF_NOT_EMPTY_VAL(x, v) ADD_IF_VAL(x, !x.empty(), v)
#define ADD_IF_NOT_EMPTY(x) ADD_IF_NOT_EMPTY_VAL(x, x)
#define ADD_IF_EQU_VAL(x, e, v) ADD_IF_VAL(x, (x) == (e), v)
#define ADD_SET(x, s) for (auto &v : s) root[#x].push_back(v)

    if (isValidSourceUrl(source))
        save_source(root, source);
    if (pkg.version.isValid() &&
        (pkg.version.type == VersionType::Version ||
            pkg.version.type == VersionType::Branch))
        root["version"] = pkg.version.toString();

    ADD_IF_NOT_EMPTY(name);
    ADD_IF_NOT_EMPTY(license);

    ADD_IF_EQU_VAL(type, ProjectType::Library, "library");
    ADD_IF_EQU_VAL(library_type, LibraryType::Shared, "shared");
    ADD_IF_EQU_VAL(library_type, LibraryType::Module, "module");
    ADD_IF_EQU_VAL(executable_type, ExecutableType::Win32, "win32");

    ADD_IF_NOT_EMPTY_VAL(root_directory, to_printable_string(normalize_path(root_directory)));
    ADD_IF_NOT_EMPTY_VAL(unpack_directory, to_printable_string(normalize_path(unpack_directory)));
    ADD_IF_NOT_EMPTY(output_directory);

    ADD_IF_NOT_EMPTY(output_name);
    ADD_IF_NOT_EMPTY(condition);
    ADD_IF_NOT_EMPTY(include_script);

    if (c_standard)
        root["c"] = c_standard;
    ADD_IF_VAL_TRIPLE(c_extensions);
    if (cxx_standard)
        root["c++"] = cxx_standard;
    ADD_IF_VAL_TRIPLE(cxx_extensions);

    ADD_IF_VAL_TRIPLE(empty);
    ADD_IF_VAL_TRIPLE(custom);

    ADD_IF_VAL_TRIPLE(static_only);
    ADD_IF_VAL_TRIPLE(shared_only);
    if (header_only)
        root["header_only"] = header_only.value();

    ADD_IF_VAL_TRIPLE(import_from_bazel);
    ADD_IF_NOT_EMPTY(bazel_target_name);
    ADD_IF_NOT_EMPTY(bazel_target_function);

    ADD_IF_VAL_TRIPLE(prefer_binaries);
    ADD_IF_VAL_TRIPLE(export_all_symbols);
    ADD_IF_VAL_TRIPLE(export_if_static);
    ADD_IF_NOT_VAL_TRIPLE(rc_enabled);
    //ADD_IF_VAL_TRIPLE(disabled);
    ADD_IF_VAL_TRIPLE(skip_on_server);
    ADD_IF_VAL_TRIPLE(create_default_api);
    ADD_IF_NOT_EMPTY(default_api_start);
    ADD_IF_VAL_TRIPLE(build_dependencies_with_same_config);
    ADD_IF_NOT_VAL_TRIPLE(copy_to_output_dir);

    ADD_SET(api_name, api_name);

    ADD_SET(files, sources);
    ADD_SET(build, build_files);
    ADD_SET(exclude_from_package, exclude_from_package);
    ADD_SET(exclude_from_build, exclude_from_build);
    ADD_SET(public_headers, public_headers);
    ADD_SET(include_hints, include_hints);

    for (auto &v : include_directories.public_)
        root["include_directories"]["public"].push_back(to_printable_string(normalize_path(v)));
    for (auto &v : include_directories.private_)
        root["include_directories"]["private"].push_back(to_printable_string(normalize_path(v)));
    for (auto &v : include_directories.interface_)
        root["include_directories"]["interface"].push_back(to_printable_string(normalize_path(v)));
    saveOptionsMap(root, options);
    ADD_SET(aliases, aliases);
    ADD_SET(checks_prefixes, checks_prefixes);
    checks.save(root);
    save_dependencies(root);
    patch.save(root);
    bs_insertions.save(root);

    return root;
}

void Project::prepareExports() const
{
    // very stupid algorithm
    auto api = CPPAN_EXPORT_PREFIX + pkg.variable_name;

    // disabled because causes self-build issues
    /*auto &srcs = getSources();
    for (auto &f : srcs)
    {
        auto s = read_file(f);

        boost::algorithm::replace_all(s, CPPAN_EXPORT, api);

        String p, e;
        std::vector<String> ev;
        for (auto &n : pkg.ppath)
        {
            p += "namespace " + n + " {\n";
            ev.push_back("} // namespace " + n + "\n");
        }
        std::reverse(ev.begin(), ev.end());
        for (auto &n : ev)
            e += n;

        boost::algorithm::replace_all(s, CPPAN_PROLOG, p);
        boost::algorithm::replace_all(s, CPPAN_EPILOG, e);

        write_file_if_different(f, s);
    }*/
}

void Project::patchSources() const
{
    patch.patchSources(*this, getSources());
}

const FilesSorted &Project::getSources() const
{
    if (!files.empty())
        return files;
    for (auto &f : fs::recursive_directory_iterator(pkg.getDirSrc()))
    {
        if (!fs::is_regular_file(f) || f.path().filename() == CPPAN_FILENAME)
            continue;
        files.insert(f);
    }
    return files;
}

void Project::setRelativePath(const String &name)
{
    pkg.ppath = relative_name_to_absolute(name);
}

void Project::applyFlags(ProjectFlags &flags) const
{
    flags.set(pfExecutable, type == ProjectType::Executable);
}

void Project::addDependency(const Package &p)
{
    auto i = dependencies.insert({ p.ppath.toString(), p });
    i.first->second.createNames();
}

OptionsMap loadOptionsMap(const yaml &root)
{
    OptionsMap options;
    if (root["options"].IsDefined() && !root["options"].IsMap())
        return options;
    get_map_and_iterate(root, "options", [&options](const auto &opt_level)
    {
        auto ol = opt_level.first.template as<String>();
        if (!(ol == "any" || ol == "static" || ol == "shared"))
            throw std::runtime_error("Wrong option level directive. Must be one of any, static or shared");
        if (!opt_level.second.IsMap())
            throw std::runtime_error("'" + ol + "' should be a map");

        auto &option = options[ol];

        auto add_opts = [](const auto &defs, const auto &s, auto &c)
        {
            if (!defs.IsDefined())
                return;
            auto dd = get_sequence_set<String, String>(defs, s);
            for (auto &d : dd)
                c.insert({ s,d });
        };
        auto add_opts_common = [&add_opts](const auto &opts, auto &c, auto &sc)
        {
            add_opts(opts, "public", c);
            add_opts(opts, "private", c);
            add_opts(opts, "interface", c);

            for (auto kv : opts)
            {
                auto f = kv.first.template as<String>();
                if (f == "public" || f == "private" || f == "interface")
                    continue;
                auto &scv = sc[f];
                add_opts(kv.second, "public", scv);
                add_opts(kv.second, "private", scv);
                add_opts(kv.second, "interface", scv);
            }
        };
        add_opts_common(opt_level.second["definitions"], option.definitions, option.system_definitions);
        add_opts_common(opt_level.second["include_directories"], option.include_directories, option.system_include_directories);
        add_opts_common(opt_level.second["compile_options"], option.compile_options, option.system_compile_options);
        add_opts_common(opt_level.second["link_options"], option.link_options, option.system_link_options);
        add_opts_common(opt_level.second["link_libraries"], option.link_libraries, option.system_link_libraries);
        add_opts_common(opt_level.second["link_directories"], option.link_directories, option.system_link_directories);

        option.bs_insertions.load(opt_level.second);
    });
    return options;
}

void saveOptionsMap(yaml &node, const OptionsMap &m)
{
    if (m.empty())
        return;

    yaml root;
    for (auto &ol : m)
    {
        auto &o = ol.second;

#define ADD_OPT(x) \
        for (auto &v : o.x) \
            root[ol.first][#x][v.first].push_back(v.second)
#define ADD_OPT_SYS(x) \
        for (auto &v1 : o.system_##x) \
            for (auto &v : v1.second) \
                root[ol.first][#x][v1.first][v.first].push_back(v.second)

        ADD_OPT(definitions);
        ADD_OPT(include_directories);
        ADD_OPT(compile_options);
        ADD_OPT(link_options);
        ADD_OPT(link_libraries);
        ADD_OPT(link_directories);

        ADD_OPT_SYS(definitions);
        ADD_OPT_SYS(include_directories);
        ADD_OPT_SYS(compile_options);
        ADD_OPT_SYS(link_options);
        ADD_OPT_SYS(link_libraries);
        ADD_OPT_SYS(link_directories);

        auto n = root[ol.first];
        o.bs_insertions.save(n);
    }
    node["options"] = root;
}

String Project::print_cpp()
{
    primitives::CppEmitter ctx;

    String name = pkg.ppath.back();

    String type = "LibraryTarget";
    if (static_only)
        type = "StaticLibraryTarget";
    else if (shared_only)
        type = "SharedLibraryTarget";
    else if (pkg.flags[pfExecutable])
        type = "ExecutableTarget";
    ctx.addLine("auto &" + name + " = addTarget<" + type + ">(s, \"" + pkg.ppath.toString() + "\", \"" + pkg.version.toString() + "\");");
    ctx.beginBlock();

    if (!checks.checks.empty())
        ctx.addLine(name + ".setChecks(\"" + name + "\");");

    if (export_all_symbols)
        ctx.addLine(name + ".ExportAllSymbols = true;");

    for (auto &n : api_name)
        ctx.addLine(name + ".ApiNames.insert(\"" + n + "\");");

    if (!sources.empty())
    {
        ctx.addLine(name + " +=");
        String s;
        for (auto &t : sources)
        {
            s += "\"" + t + "\"";
            if (t.find("\\") != String::npos || t.find("*") != String::npos)
                s += "_rr";
            s += ",\n";
        }
        boost::replace_all(s, "\\", "\\\\");
        s.resize(s.size() - 2);
        s += ";\n";
        ctx.addLine(s);
    }

    if (!exclude_from_build.empty())
    {
        ctx.addLine(name + " -=");
        String s;
        for (auto &t : exclude_from_build)
        {
            s += "\"" + t + "\"";
            if (t.find("\\") != String::npos || t.find("*") != String::npos)
                s += "_rr";
            s += ",\n";
        }
        boost::replace_all(s, "\\", "\\\\");
        s.resize(s.size() - 2);
        s += ";\n";
        ctx.addLine(s);
    }

    /*if (!exclude_from_package.empty())
    {
        ctx.addLine(name + " -=");
        String s;
        for (auto &t : exclude_from_package)
        {
            s += "\"" + t + "\"";
            if (t.find("\\") != String::npos || t.find("*") != String::npos)
                s += "_rr";
            s += ",\n";
        }
        boost::replace_all(s, "\\", "\\\\");
        s.resize(s.size() - 2);
        s += ";\n";
        ctx.addLine(s);
    }*/

    if (include_directories.private_.size() > additional_include_dirs_private.size())
    {
        ctx.addLine(name + " +=");
        String s;
        for (auto &t : include_directories.private_)
        {
            if (t.string().find("BDIR") == String::npos)
                s += "\"" + t.string() + "\"_id,\n";
        }
        s.resize(s.size() - 2);
        s += ";\n";
        ctx.addLine(s);
    }

    if (include_directories.public_.size() > additional_include_dirs_public.size())
    {
        ctx.addLine(name + ".Public +=");
        String s;
        for (auto &t : include_directories.public_)
        {
            if (t.string().find("BDIR") == String::npos)
                s += "\"" + t.string() + "\"_id,\n";
        }
        s.resize(s.size() - 2);
        s += ";\n";
        ctx.addLine(s);
    }

    if (!include_directories.interface_.empty())
    {
        ctx.addLine(name + ".Interface +=");
        String s;
        for (auto &t : include_directories.interface_)
            s += "\"" + t.string() + "\"_id,\n";
        s.resize(s.size() - 2);
        s += ";\n";
        ctx.addLine(s);
    }

    auto any = options.find("any");
    if (any != options.end())
    {
        auto print_def = [&ctx, &name](auto &k, auto &v)
        {
            if (k == "private")
                ctx.addLine(name + " += \"" + v + "\"_d;");
            else if (k == "public")
                ctx.addLine(name + ".Public += \"" + v + "\"_d;");
            else if (k == "interface")
                ctx.addLine(name + ".Interface += \"" + v + "\"_d;");
        };

        if (!any->second.definitions.empty())
        {
            for (auto &[k, v] : any->second.definitions)
                print_def(k, v);
        }
        if (!any->second.system_definitions.empty())
        {
            for (auto &[k2, v2] : any->second.system_definitions)
            {
                if (k2 == "win32")
                {
                    ctx.beginBlock("if (s.Settings.TargetOS.Type == OSType::Windows)");
                    for (auto &[k, v] : v2)
                        print_def(k, v);
                    ctx.endBlock();
                }
                else if (k2 == "unix")
                {
                    ctx.beginBlock("if (s.Settings.TargetOS.Type != OSType::Windows)");
                    for (auto &[k, v] : v2)
                        print_def(k, v);
                    ctx.endBlock();
                }
            }
        }
    }

    auto shared = options.find("shared");
    if (shared != options.end())
    {
        auto print_def = [&ctx, &name](auto &k, auto &v)
        {
            if (k == "private")
                ctx.addLine(name + " += sw::Shared, \"" + v + "\"_d;");
            else if (k == "public")
                ctx.addLine(name + ".Public += sw::Shared, \"" + v + "\"_d;");
            else if (k == "interface")
                ctx.addLine(name + ".Interface += sw::Shared, \"" + v + "\"_d;");
        };

        if (!shared->second.definitions.empty())
        {
            for (auto &[k, v] : shared->second.definitions)
                print_def(k, v);
        }
        if (!shared->second.system_definitions.empty())
        {
            for (auto &[k2, v2] : shared->second.system_definitions)
            {
                if (k2 == "win32")
                {
                    ctx.beginBlock("if (s.Settings.TargetOS.Type == OSType::Windows)");
                    for (auto &[k, v] : v2)
                        print_def(k, v);
                    ctx.endBlock();
                }
                else if (k2 == "unix")
                {
                    ctx.beginBlock("if (s.Settings.TargetOS.Type != OSType::Windows)");
                    for (auto &[k, v] : v2)
                        print_def(k, v);
                    ctx.endBlock();
                }
            }
        }
    }

    auto static_ = options.find("static");
    if (static_ != options.end())
    {
        auto print_def = [&ctx, &name](auto &k, auto &v)
        {
            if (k == "private")
                ctx.addLine(name + " += sw::Static, \"" + v + "\"_d;");
            else if (k == "public")
                ctx.addLine(name + ".Public += sw::Static, \"" + v + "\"_d;");
            else if (k == "interface")
                ctx.addLine(name + ".Interface. += sw::Static, \"" + v + "\"_d;");
        };

        if (!static_->second.definitions.empty())
        {
            for (auto &[k, v] : static_->second.definitions)
                print_def(k, v);
        }
        if (!static_->second.system_definitions.empty())
        {
            for (auto &[k2, v2] : static_->second.system_definitions)
            {
                if (k2 == "win32")
                {
                    ctx.beginBlock("if (s.Settings.TargetOS.Type == OSType::Windows)");
                    for (auto &[k, v] : v2)
                        print_def(k, v);
                    ctx.endBlock();
                }
                else if (k2 == "unix")
                {
                    ctx.beginBlock("if (s.Settings.TargetOS.Type != OSType::Windows)");
                    for (auto &[k, v] : v2)
                        print_def(k, v);
                    ctx.endBlock();
                }
            }
        }
    }

    ctx.emptyLines();

    if (!dependencies.empty())
    {
        String s;
        for (auto &d : dependencies)
            s += d.second.ppath.back() + ", ";
        s.resize(s.size() - 2);
        ctx.addLine(name + ".Public += " + s + ";");
    }

    ctx.endBlock();
    ctx.addLine();

    if (!checks.checks.empty())
    {
        ctx.beginBlock();
        ctx.addLine("auto &s = c.addSet(\"" + name + "\");");
        for (auto &c : checks.checks)
        {
            switch (c->getInformation().type)
            {
            case Check::Function:
                ctx.addLine("s.checkFunctionExists(\"" + c->getData() + "\");");
                break;
            case Check::Include:
                ctx.addLine("s.checkIncludeExists(\"" + c->getData() + "\");");
                //if (c->get_cpp())
                break;
            case Check::Type:
                ctx.addLine("s.checkTypeSize(\"" + c->getData() + "\");");
                break;
            case Check::Decl:
                ctx.addLine("s.checkDeclarationExists(\"" + c->getData() + "\");");
                break;
            case Check::LibraryFunction:
                ctx.addLine("s.checkLibraryFunctionExists(\"" + ((CheckLibraryFunction*)c.get())->library + "\", \"" + c->getData() + "\");");
                break;
            case Check::CSourceCompiles:
                ctx.addLine("s.checkSourceCompiles(\"" + c->getVariable() + "\", R\"sw_xxx(" + c->getData() + ")sw_xxx\");");
                break;
            case Check::StructMember:
                ctx.beginBlock();
                ctx.addLine("auto &c = s.checkStructMemberExists(\"" + ((CheckStructMember*)c.get())->struct_ + "\", \"" + c->getData() + "\");");
                for (auto &i : c->parameters.headers)
                    ctx.addLine("c.Parameters.Includes.push_back(\"" + i + "\");");
                ctx.endBlock();
                break;
            case Check::Symbol:
                ctx.beginBlock();
                ctx.addLine("auto &c = s.checkSymbolExists(\"" + c->getData() + "\");");
                for (auto &i : c->parameters.headers)
                    ctx.addLine("c.Parameters.Includes.push_back(\"" + i + "\");");
                ctx.endBlock();
                break;
            }
        }
        ctx.endBlock();
    }

    return ctx.getText();
}

void print_checks2(primitives::CppEmitter &ctx, const ChecksSet &checks, const String &name);

String Project::print_cpp2()
{
    primitives::CppEmitter ctx;

    String name = pkg.ppath.back();

    String type = "LibraryTarget";
    if (static_only)
        type = "StaticLibraryTarget";
    else if (shared_only)
        type = "SharedLibraryTarget";
    else if (pkg.flags[pfExecutable])
        type = "ExecutableTarget";

    ctx.beginBlock("void build(Solution &s)");
    ctx.addLine("auto &" + name + " = s.addTarget<" + type + ">(\"" + pkg.ppath.slice(3).toString() + "\", \"" + pkg.version.toString() + "\");");
    auto src = print_source_cpp(source);
    ctx.addLine(name + " += " + src + ";");
    ctx.emptyLines();

    if (checks.checks.size() > 2)
    {
        ctx.addLine(name + ".setChecks(\"" + name + "\");");
        ctx.emptyLines();
    }

    if (export_all_symbols)
        ctx.addLine(name + ".ExportAllSymbols = true;");

    for (auto &n : api_name)
        ctx.addLine(name + ".ApiNames.insert(\"" + n + "\");");

    if (!sources.empty())
    {
        ctx.addLine(name + " +=");
        String s;
        for (auto &t : sources)
        {
            s += "\"" + t + "\"";
            if (t.find("\\") != String::npos || t.find("*") != String::npos)
                s += "_rr";
            s += ",\n";
        }
        boost::replace_all(s, "\\", "\\\\");
        s.resize(s.size() - 2);
        s += ";\n";
        ctx.increaseIndent();
        ctx.addLine(s);
        ctx.decreaseIndent();
    }

    if (!exclude_from_build.empty())
    {
        ctx.addLine(name + " -=");
        String s;
        for (auto &t : exclude_from_build)
        {
            s += "\"" + t + "\"";
            if (t.find("\\") != String::npos || t.find("*") != String::npos)
                s += "_rr";
            s += ",\n";
        }
        boost::replace_all(s, "\\", "\\\\");
        s.resize(s.size() - 2);
        s += ";\n";
        ctx.increaseIndent();
        ctx.addLine(s);
        ctx.decreaseIndent();
    }

    if (!exclude_from_package.empty())
    {
        ctx.addLine(name + " -=");
        String s;
        for (auto &t : exclude_from_package)
        {
            s += "\"" + t + "\"";
            if (t.find("\\") != String::npos || t.find("*") != String::npos)
                s += "_rr";
            s += ",\n";
        }
        boost::replace_all(s, "\\", "\\\\");
        s.resize(s.size() - 2);
        s += ";\n";
        ctx.increaseIndent();
        ctx.addLine(s);
        ctx.decreaseIndent();
    }

    if (include_directories.private_.size() > additional_include_dirs_private.size())
    {
        ctx.addLine(name + " +=");
        String s;
        for (auto &t : include_directories.private_)
        {
            if (t.string().find("BDIR") == String::npos)
                s += "\"" + t.string() + "\"_id,\n";
        }
        s.resize(s.size() - 2);
        s += ";\n";
        ctx.increaseIndent();
        ctx.addLine(s);
        ctx.decreaseIndent();
    }

    if (include_directories.public_.size() > additional_include_dirs_public.size())
    {
        String s;
        for (auto &t : include_directories.public_)
        {
            auto str = t.string();
            if (str.find("BDIR") != String::npos)
                continue;
            if (str == ".")
                continue;
            s += "\"" + str + "\"_id,\n";
        }
        if (!s.empty())
        {
            s.resize(s.size() - 2);
            s += ";\n";
            ctx.addLine(name + ".Public +=");
            ctx.increaseIndent();
            ctx.addLine(s);
            ctx.decreaseIndent();
        }
    }

    if (!include_directories.interface_.empty())
    {
        ctx.addLine(name + ".Interface +=");
        String s;
        for (auto &t : include_directories.interface_)
            s += "\"" + t.string() + "\"_id,\n";
        s.resize(s.size() - 2);
        s += ";\n";
        ctx.increaseIndent();
        ctx.addLine(s);
        ctx.decreaseIndent();
    }

    auto escape_str = [](auto s)
    {
        boost::replace_all(s, "\\", "\\\\");
        boost::replace_all(s, "\"", "\\\"");
        return s;
    };

    auto print_def = [&ctx, &name, &escape_str](auto &k, auto &v, const String &ending, const String &type = {})
    {
        if (k == "private")
            ctx.addLine(name + " += " + (type.empty() ? "" : (type + ", ")) + "\"" + escape_str(v) + "\"_" + ending + ";");
        else if (k == "public")
            ctx.addLine(name + ".Public += " + (type.empty() ? "" : (type + ", ")) + "\"" + escape_str(v) + "\"_" + ending + ";");
        else if (k == "interface")
            ctx.addLine(name + ".Interface += " + (type.empty() ? "" : (type + ", ")) + "\"" + escape_str(v) + "\"_" + ending + ";");
    };

    auto print_group = [this, &print_def, &ctx](const String &gn, const String &type = {})
    {
        auto g = options.find(gn);
        if (g == options.end())
            return;

        auto print_obj = [&ctx, &type](auto &obj, auto &sysobj, auto f, const String &ending)
        {
            for (auto &p : obj)
                f(p.first, p.second, ending, type);
            for (auto &p2 : sysobj)
            {
                if (p2.first == "win32")
                {
                    ctx.beginBlock("if (s.Settings.TargetOS.Type == OSType::Windows)");
                    for (auto &p : p2.second)
                        f(p.first, p.second, ending);
                    ctx.endBlock();
                }
                else if (p2.first == "unix")
                {
                    ctx.beginBlock("if (s.Settings.TargetOS.Type != OSType::Windows)");
                    for (auto &p : p2.second)
                        f(p.first, p.second, ending);
                    ctx.endBlock();
                }
                else if (p2.first == "msvc")
                {
                    ctx.beginBlock("if (s.Settings.Native.CompilerType == CompilerType::MSVC)");
                    for (auto &p : p2.second)
                        f(p.first, p.second, ending);
                    ctx.endBlock();
                }
            }
        };

        print_obj(g->second.definitions, g->second.system_definitions, print_def, "d");
        print_obj(g->second.compile_options, g->second.system_compile_options, print_def, "xxx");
        print_obj(g->second.link_options, g->second.system_link_options, print_def, "xxx");
        print_obj(g->second.link_libraries, g->second.system_link_libraries, print_def, "lib");
        print_obj(g->second.link_directories, g->second.system_link_directories, print_def, "ld");
    };

    print_group("any");
    print_group("shared", "sw::Shared");
    print_group("static", "sw::Static");

    ctx.emptyLines();

    for (auto &d : dependencies)
    {
        auto pp = d.second.ppath.toString();
        boost::replace_all(pp, "pvt.cppan", "org.sw");
        auto vv = d.second.version.toAnyVersion();
        if (vv != "*")
            vv = "-" + vv;
        else
            vv.clear();
        if (d.second.flags[pfPrivateDependency])
            ctx.addLine(name + " += \"" + pp + vv + "\"_dep;");
        else
            ctx.addLine(name + ".Public += \"" + pp + vv + "\"_dep;");
    }

    ctx.endBlock();
    ctx.emptyLines();

    print_checks2(ctx, checks.checks, name);

    //ctx.splitLines();
    return ctx.getText();
}

void print_checks2(primitives::CppEmitter &ctx, const ChecksSet &checks, const String &name)
{
    if (checks.size() > 2)
    {
        ctx.beginBlock("void check(Checker &c)");
        ctx.addLine("auto &s = c.addSet(\"" + name + "\");");
        for (auto &c : checks)
        {
            switch (c->getInformation().type)
            {
            case Check::Function:
                ctx.addLine("s.checkFunctionExists(\"" + c->getData() + "\");");
                break;
            case Check::Include:
                ctx.addLine("s.checkIncludeExists(\"" + c->getData() + "\");");
                //if (c->get_cpp())
                break;
            case Check::Type:
                ctx.addLine("s.checkTypeSize(\"" + c->getData() + "\");");
                break;
            case Check::Decl:
                ctx.addLine("s.checkDeclarationExists(\"" + c->getData() + "\");");
                break;
            case Check::Alignment:
                ctx.addLine("s.checkTypeAlignment(\"" + c->getData() + "\");");
                break;
            case Check::LibraryFunction:
                ctx.addLine("s.checkLibraryFunctionExists(\"" + ((CheckLibraryFunction*)c.get())->library + "\", \"" + c->getData() + "\");");
                break;
            case Check::CSourceCompiles:
                ctx.addLine("s.checkSourceCompiles(\"" + c->getVariable() + "\", R\"sw_xxx(" + c->getData() + ")sw_xxx\");");
                break;
            case Check::StructMember:
                ctx.beginBlock();
                ctx.addLine("auto &c = s.checkStructMemberExists(\"" + ((CheckStructMember*)c.get())->struct_ + "\", \"" + c->getData() + "\");");
                for (auto &i : c->parameters.headers)
                    ctx.addLine("c.Parameters.Includes.push_back(\"" + i + "\");");
                ctx.endBlock();
                break;
            case Check::Symbol:
                ctx.beginBlock();
                ctx.addLine("auto &c = s.checkSymbolExists(\"" + c->getData() + "\");");
                for (auto &i : c->parameters.headers)
                    ctx.addLine("c.Parameters.Includes.push_back(\"" + i + "\");");
                ctx.endBlock();
                break;
            }
        }
        ctx.endBlock();
    }
}
