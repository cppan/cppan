#pragma sw require header pub.egorpugin.primitives.tools.embedder2
#pragma sw require header org.sw.demo.lexxmark.winflexbison.bison

void build(Solution &s)
{
    auto &p = s.addProject("cppan", "0.2.5");
    p += Git("https://github.com/cppan/cppan", "", "v1");

    auto &common = p.addTarget<StaticLibraryTarget>("common");
    {
        common += cpp20;
        common +=
            "src/common/.*"_rr,
            "src/printers/.*"_rr,
            "src/comments/.*"_rr,
            "src/bazel/.*"_rr,
            "src/inserts/.*"_rr,
            "src/support/.*"_rr,
            "src/gen/.*"_rr;

        common -= "src/bazel/test/test.cpp", "src/gen/.*"_rr;
        common.Public += "src"_id, "src/common"_id, "src/support"_id;

        common.Public += "VERSION_MAJOR=0"_d;
        common.Public += "VERSION_MINOR=2"_d;
        common.Public += "VERSION_PATCH=5"_d;
        common.Public += "BUILD_NUMBER=0"_d;
        common.Public += "CPPAN_VERSION_STRING=0.2.5"_d;
        if (common.getBuildSettings().TargetOS.Type == OSType::Windows)
            common.Public += "UNICODE"_d;

        common.Public +=
            "org.sw.demo.boost.optional"_dep,
            "org.sw.demo.boost.property_tree"_dep,
            "org.sw.demo.boost.variant"_dep,
            "org.sw.demo.boost.stacktrace"_dep,
            "org.sw.demo.sqlite3"_dep,
            "org.sw.demo.fmt"_dep,
            "org.sw.demo.imageworks.pystring"_dep,
            "org.sw.demo.giovannidicanio.winreg-master"_dep,

            "pub.egorpugin.primitives.string"_dep,
            "pub.egorpugin.primitives.filesystem"_dep,
            "pub.egorpugin.primitives.emitter"_dep,
            "pub.egorpugin.primitives.date_time"_dep,
            "pub.egorpugin.primitives.executor"_dep,
            "pub.egorpugin.primitives.hash"_dep,
            "pub.egorpugin.primitives.http"_dep,
            "pub.egorpugin.primitives.lock"_dep,
            "pub.egorpugin.primitives.log"_dep,
            "pub.egorpugin.primitives.pack"_dep,
            "pub.egorpugin.primitives.patch"_dep,
            "pub.egorpugin.primitives.command"_dep,
            "pub.egorpugin.primitives.win32helpers"_dep,
            "pub.egorpugin.primitives.yaml"_dep;

        time_t v;
        time(&v);
        common.writeFileSafe("stamp.h.in", "\"" + std::to_string(v) + "\"");
        embed2("pub.egorpugin.primitives.tools.embedder2"_dep, common, "src/inserts/cppan.h");
        embed2("pub.egorpugin.primitives.tools.embedder2"_dep, common, "src/inserts/branch.rc.in");
        embed2("pub.egorpugin.primitives.tools.embedder2"_dep, common, "src/inserts/version.rc.in");
        embed2("pub.egorpugin.primitives.tools.embedder2"_dep, common, "src/inserts/functions.cmake");
        embed2("pub.egorpugin.primitives.tools.embedder2"_dep, common, "src/inserts/build.cmake");
        embed2("pub.egorpugin.primitives.tools.embedder2"_dep, common, "src/inserts/generate.cmake");
        embed2("pub.egorpugin.primitives.tools.embedder2"_dep, common, "src/inserts/exports.cmake");
        embed2("pub.egorpugin.primitives.tools.embedder2"_dep, common, "src/inserts/header.cmake");
        embed2("pub.egorpugin.primitives.tools.embedder2"_dep, common, "src/inserts/CPPANConfig.cmake");

        auto [fc, bc] = gen_flex_bison("org.sw.demo.lexxmark.winflexbison"_dep, common,
            "src/bazel/bazel.ll", "src/bazel/bazel.yy",
            { "--prefix=ll_bazel", "--header-file=" + to_printable_string(normalize_path(common.BinaryDir / "bazel/lexer.h")) });
        if (!common.DryRun)
            fc->addOutput(common.BinaryDir / "bazel/lexer.h");

        auto [fc2, bc2] = gen_flex_bison("org.sw.demo.lexxmark.winflexbison"_dep, common,
            "src/comments/comments.ll", "src/comments/comments.yy",
            { "--prefix=ll_comments", "--header-file=" + to_printable_string(normalize_path(common.BinaryDir / "comments/lexer.h")) });
        if (!common.DryRun)
            fc2->addOutput(common.BinaryDir / "comments/lexer.h");
    }

    auto &client = p.addTarget<ExecutableTarget>("client");
    {
        client.PackageDefinitions = true;
        client += cpp20;

        // for rc.exe
        client += "VERSION_MAJOR=0"_d;
        client += "VERSION_MINOR=2"_d;
        client += "VERSION_PATCH=5"_d;
        client += "BUILD_NUMBER=0"_d;
        client += "CPPAN_VERSION_STRING=0.2.5"_d;

        client += "src/client/.*"_rr, common,
            "pub.egorpugin.primitives.sw.main"_dep,
            "org.sw.demo.boost.program_options"_dep,
            "org.sw.demo.yhirose.cpp_linenoise-master"_dep;
    }
}
