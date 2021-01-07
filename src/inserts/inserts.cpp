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

#include "inserts.h"

#define DECLARE_TEXT_VAR_BEGIN(x) const char _##x[] = {
#define DECLARE_TEXT_VAR_END(x) }; const String x = _##x;

DECLARE_TEXT_VAR_BEGIN(cppan_h)
#include <src/inserts/cppan.h.emb>
DECLARE_TEXT_VAR_END(cppan_h);

DECLARE_TEXT_VAR_BEGIN(branch_rc_in)
#include <src/inserts/branch.rc.in.emb>
DECLARE_TEXT_VAR_END(branch_rc_in);

DECLARE_TEXT_VAR_BEGIN(version_rc_in)
#include <src/inserts/version.rc.in.emb>
DECLARE_TEXT_VAR_END(version_rc_in);

DECLARE_TEXT_VAR_BEGIN(cmake_functions)
#include <src/inserts/functions.cmake.emb>
DECLARE_TEXT_VAR_END(cmake_functions);

DECLARE_TEXT_VAR_BEGIN(cmake_build_file)
#include <src/inserts/build.cmake.emb>
DECLARE_TEXT_VAR_END(cmake_build_file);

DECLARE_TEXT_VAR_BEGIN(cmake_generate_file)
#include <src/inserts/generate.cmake.emb>
DECLARE_TEXT_VAR_END(cmake_generate_file);

DECLARE_TEXT_VAR_BEGIN(cmake_export_import_file)
#include <src/inserts/exports.cmake.emb>
DECLARE_TEXT_VAR_END(cmake_export_import_file);

DECLARE_TEXT_VAR_BEGIN(cmake_header)
#include <src/inserts/header.cmake.emb>
DECLARE_TEXT_VAR_END(cmake_header);

DECLARE_TEXT_VAR_BEGIN(cppan_cmake_config)
#include <src/inserts/CPPANConfig.cmake.emb>
DECLARE_TEXT_VAR_END(cppan_cmake_config);

#undef DECLARE_TEXT_VAR_BEGIN
#undef DECLARE_TEXT_VAR_END
