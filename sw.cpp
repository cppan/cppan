#pragma sw require header pub.egorpugin.primitives.tools.embedder-master
//#pragma sw require header org.sw.demo.lexxmark.winflexbison.bison-master

void configure(Build &s)
{
    s.Settings.Native.LibrariesType = LibraryType::Static;
    s.Settings.Native.ConfigurationType = ConfigurationType::ReleaseWithDebugInformation;
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
    if (s.Settings.TargetOS.Type == OSType::Windows)
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
        "pub.egorpugin.primitives.context-master"_dep,
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

    // at the moment flex&bison generated files are present in the build tree,
    // so build passes without generation stage

    /*auto flex_bison = [&common](const std::string &name)
    {
        fs::create_directories(common.BinaryDir / ("src/" + name));

        // flex/bison
        {
            auto c = std::make_shared<Command>();
            c->program = "bison.exe";
            c->args.push_back("-d");
            c->args.push_back("-o" + (common.BinaryDir / ("src/" + name + "/grammar.cpp")).string());
            c->args.push_back((common.SourceDir / ("src/" + name + "/grammar.yy")).string());
            c->addInput(common.SourceDir / ("src/" + name + "/grammar.yy"));
            c->addOutput(common.BinaryDir / ("src/" + name + "/grammar.cpp"));
            common += path(common.BinaryDir / ("src/" + name + "/grammar.cpp"));
        }
        {
            auto c = std::make_shared<Command>();
            c->program = "flex.exe";
            c->args.push_back("--header-file=" + (common.BinaryDir / ("src/" + name + "/lexer.h")).string());
            c->args.push_back("-o" + (common.BinaryDir / ("src/" + name + "/lexer.cpp")).string());
            c->args.push_back((common.SourceDir / ("src/" + name + "/lexer.ll")).string());
            c->addInput(common.SourceDir / ("src/" + name + "/lexer.ll"));
            c->addOutput(common.BinaryDir / ("src/" + name + "/lexer.h"));
            c->addOutput(common.BinaryDir / ("src/" + name + "/lexer.cpp"));
            common += path(common.BinaryDir / ("src/" + name + "/lexer.cpp"));
        }
    };*/

    //flex_bison("bazel");
    //flex_bison("comments");

    auto &client = p.addTarget<ExecutableTarget>("client");
    client.CPPVersion = CPPLanguageStandard::CPP17;
    client += "src/client/.*"_rr, common,
        "pub.egorpugin.primitives.sw.main-master"_dep,
        "org.sw.demo.boost.program_options-1"_dep,
        "org.sw.demo.yhirose.cpp_linenoise-master"_dep;
}
