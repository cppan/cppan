#pragma sw require header pub.egorpugin.primitives.tools.embedder-master
#pragma sw require header org.sw.demo.lexxmark.winflexbison.bison

void configure(Build &s)
{
    /*auto ss = s.createSettings();
    ss.Native.LibrariesType = LibraryType::Static;
    ss.Native.ConfigurationType = ConfigurationType::ReleaseWithDebugInformation;
    s.addSettings(ss);*/
}

void build(Solution &s)
{
    auto &p = s.addProject("cppan", "master");
    p += Git("https://github.com/cppan/cppan", "", "v1");

    auto &common = p.addTarget<StaticLibraryTarget>("common");
    common.CPPVersion = CPPLanguageStandard::CPP17;
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
        "org.sw.demo.boost.optional-1"_dep,
        "org.sw.demo.boost.property_tree-1"_dep,
        "org.sw.demo.boost.variant-1"_dep,
        "org.sw.demo.boost.stacktrace-1"_dep,
        //"org.sw.demo.apolukhin.stacktrace-master"_dep,
        "org.sw.demo.sqlite3-3"_dep,
        "org.sw.demo.fmt-*"_dep,
        "org.sw.demo.imageworks.pystring-1"_dep,
        "org.sw.demo.giovannidicanio.winreg-master"_dep,

        "pub.egorpugin.primitives.string-master"_dep,
        "pub.egorpugin.primitives.filesystem-master"_dep,
        "pub.egorpugin.primitives.emitter-master"_dep,
        "pub.egorpugin.primitives.date_time-master"_dep,
        "pub.egorpugin.primitives.executor-master"_dep,
        "pub.egorpugin.primitives.hash-master"_dep,
        "pub.egorpugin.primitives.http-master"_dep,
        "pub.egorpugin.primitives.lock-master"_dep,
        "pub.egorpugin.primitives.log-master"_dep,
        "pub.egorpugin.primitives.pack-master"_dep,
        "pub.egorpugin.primitives.patch-master"_dep,
        "pub.egorpugin.primitives.command-master"_dep,
        "pub.egorpugin.primitives.win32helpers-master"_dep,
        "pub.egorpugin.primitives.yaml-master"_dep;

    time_t v;
    time(&v);
    common.writeFileSafe("stamp.h.in", "\"" + std::to_string(v) + "\"");
    embed("pub.egorpugin.primitives.tools.embedder-master"_dep, common, "src/inserts/inserts.cpp.in");

    auto [fc,bc] = gen_flex_bison("org.sw.demo.lexxmark.winflexbison"_dep, common,
        "src/bazel/bazel.ll", "src/bazel/bazel.yy",
                   {"--prefix=ll_bazel", "--header-file=" + normalize_path(common.BinaryDir / "bazel/lexer.h")});
    fc->addOutput(common.BinaryDir / "bazel/lexer.h");

    auto [fc2,bc2] = gen_flex_bison("org.sw.demo.lexxmark.winflexbison"_dep, common,
        "src/comments/comments.ll", "src/comments/comments.yy",
                   {"--prefix=ll_comments", "--header-file=" + normalize_path(common.BinaryDir / "comments/lexer.h")});
    fc2->addOutput(common.BinaryDir / "comments/lexer.h");

    auto &client = p.addTarget<ExecutableTarget>("client");
    client.CPPVersion = CPPLanguageStandard::CPP17;

    // for rc.exe
    client += "VERSION_MAJOR=0"_d;
    client += "VERSION_MINOR=2"_d;
    client += "VERSION_PATCH=5"_d;
    client += "BUILD_NUMBER=0"_d;
    client += "CPPAN_VERSION_STRING=0.2.5"_d;

    client += "src/client/.*"_rr, common,
        "pub.egorpugin.primitives.sw.main-master"_dep,
        "org.sw.demo.boost.program_options-1"_dep,
        "org.sw.demo.yhirose.cpp_linenoise-master"_dep;
}
