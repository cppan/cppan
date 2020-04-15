/*
 * SW - Build System and Package Manager
 * Copyright (C) 2017-2020 Egor Pugin
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

#pragma once

#include <primitives/filesystem.h>

#include <optional>

namespace sw
{

struct InputDatabase;
struct SwContext;

struct SW_CORE_API SpecificationFile
{
    path absolute_path;
    std::optional<String> contents;
};

struct SW_CORE_API SpecificationFiles
{
    // For inline spec we may pass virtual file name and actual contents
    // that cannot be read from fs.
    // Example, inline cppan.yml: addFile(someroot, "cppan.yml", extracted yml contents from comments);
    // relative_path - path relative to package root, may be virtual
    // absolute_path - path on disk, may differ from relative, example: main.cpp where we take inline cppan.yml from
    void addFile(const path &relative_path, const path &absolute_path, const std::optional<String> &contents = std::optional<String>{});

    const std::map<path, SpecificationFile> &getData() const { return data; }
    fs::file_time_type getLastWriteTime() const;

private:
    //       rel.
    std::map<path, SpecificationFile> data;
};

// Represents set of specification files for single input.
// It may be set of sw (make, cmake, qmake etc.) files.
//
// must store one of:
//  - set of files (including virtual)
//  - single directory
struct SW_CORE_API Specification
{
    Specification(const SpecificationFiles &);
    Specification(const path &dir);

    // One spec differs from the other by its hash.
    // We only need to test it locally and do not care about portability between systems.
    // Hash is combination of rel paths and contents.
    size_t getHash(const InputDatabase &) const;

    bool isOutdated(const fs::file_time_type &) const;

    //const String &getFileContents(const path &relpath);
    //const String &getFileContents(const path &relpath) const;

    // returns absolute paths of files
    Files getFiles() const;

    String getName() const;

//private: // temporarily (TODO: update upload)
    SpecificationFiles files;
    path dir;
};

} // namespace sw