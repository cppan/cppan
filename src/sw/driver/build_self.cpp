// Copyright (C) 2017-2018 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#define SW_PACKAGE_API
#include <sw/driver/sw.h>

#include "solution.h"

#include <sw/builder/sw_context.h>
#include <sw/manager/resolver.h>

#include <boost/algorithm/string.hpp>
#include <primitives/executor.h>

// disable custom pragma warnings
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4005) // warning C4005: 'XXX': macro redefinition
#endif

#include <build_self.generated.h>

namespace sw
{

void check_self(Checker &c)
{
    check_self_generated(c);
}

void build_self(Solution &s)
{
#include <build_self.packages.generated.h>

    //static UnresolvedPackages store; // tmp store
    auto m = s.swctx.resolve(required_packages/*, store*/);
    auto &e = getExecutor();
    for (auto &[u, p] : m)
    {
        e.push([&p] { p.install(); });
        s.knownTargets.insert(p);
    }
    e.wait();

    s.Settings.Native.LibrariesType = LibraryType::Static;
    s.Variables["SW_SELF_BUILD"] = 1;

    SwapAndRestore sr(s.Local, false);
    build_self_generated(s);
}

} // namespace sw
