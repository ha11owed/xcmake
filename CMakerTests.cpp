#ifdef CMAKER_WITH_UNIT_TESTS
#include "CMaker.h"

#include "Config.h"
#include "file_system.h"
#include "loguru.hpp"
#include "gtest/gtest.h"

namespace gatools {

static std::string cmakerJson;
static std::string inputCbp;

class CMakerTests : public ::testing::Test {
  public:
    CMakerTests() {
        loguru::g_stderr_verbosity = loguru::Verbosity_OFF;

        if (cmakerJson.empty()) {
            ga::readFile("cmaker.json", cmakerJson);
        }
        if (inputCbp.empty()) {
            ga::readFile("testproject_input.cbp", inputCbp);
        }
    }

    CMaker cmaker;
};

TEST_F(CMakerTests, CMAKE_BASH) {
    std::string tmpDir = "/tmp/xcmake/test";
    mkdir("/tmp/xcmake/", S_IRWXU);
    mkdir("/tmp/xcmake/test", S_IRWXU);
    ga::writeFile(ga::combine(tmpDir, "cmaker.json"), cmakerJson);
    std::string projectDir = ga::combine(tmpDir, "proj42");
    std::string buildDir = ga::combine(tmpDir, "build");
    mkdir(projectDir.c_str(), S_IRWXU);
    mkdir(buildDir.c_str(), S_IRWXU);

    std::string sdkDir = "/home/alin/sdks42";

    CmdLineArgs cmdLineArgs;
    cmdLineArgs.args = {"xcmake", projectDir, "'-GCodeBlocks - Unix Makefiles'"};
    cmdLineArgs.pwd = buildDir;
    cmdLineArgs.home = tmpDir;
    int r = cmaker.init(cmdLineArgs);
    ASSERT_EQ(0, r);

    // Check the Execution plan: env and cmd are updated
    {
        const ExecutionPlan *ep = cmaker.getExecutionPlan();
        const auto &env = ep->cmdLineArgs.env;
        ASSERT_EQ(1, env.size());
        ASSERT_TRUE(std::find(env.begin(), env.end(), "E1=1") != env.end());

        ASSERT_EQ(sdkDir + "/usr/bin/cmaker", ep->exePath);

        const auto &args = ep->cmdLineArgs.args;
        ASSERT_EQ(cmdLineArgs.args.size(), args.size());
        ASSERT_EQ(sdkDir + "/usr/bin/cmake", args[0]);
        for (size_t i = 1; i < args.size(); i++) {
            ASSERT_EQ(cmdLineArgs.args[i], args[i]);
        }

        ASSERT_EQ(cmdLineArgs.pwd, ep->cmdLineArgs.pwd);
        ASSERT_EQ(cmdLineArgs.home, ep->cmdLineArgs.home);

        ASSERT_EQ(buildDir, ep->buildDir);
        ASSERT_EQ(projectDir, ep->projectDir);
        ASSERT_EQ(sdkDir, ep->sdkDir);
        ASSERT_EQ(1, ep->extraAddDirectory.size());
        ASSERT_EQ(1, ep->gccClangFixes.size());

        ASSERT_EQ(1, ep->cbpSearchPaths.size());
        ASSERT_EQ(buildDir, ep->cbpSearchPaths[0]);
        ASSERT_EQ(1, ep->output.size());
    }

    // we skip the run and just write the
    ga::writeFile(buildDir + "/proj42.cbp", inputCbp);

    r = cmaker.patch();
    ASSERT_EQ(0, r);
    std::string actualCbp;
    ga::readFile(buildDir + "/proj42.cbp", actualCbp);
    ASSERT_NE(actualCbp, inputCbp);

    // Execute bash in the build directory
    cmdLineArgs.args = {"xbash"};
    cmdLineArgs.pwd = ga::combine(buildDir, "some_dir");
    cmdLineArgs.home = tmpDir;
    r = cmaker.init(cmdLineArgs);
    ASSERT_EQ(0, r);

    // Check the Execution plan: env and cmd are updated
    {
        const ExecutionPlan *ep = cmaker.getExecutionPlan();
        const auto &env = ep->cmdLineArgs.env;
        ASSERT_EQ(1, env.size());
        ASSERT_TRUE(std::find(env.begin(), env.end(), "E1=1") != env.end());

        ASSERT_EQ(sdkDir + "/usr/bin/basher", ep->exePath);

        const auto &args = ep->cmdLineArgs.args;
        ASSERT_EQ(cmdLineArgs.args.size(), args.size());
        ASSERT_EQ(sdkDir + "/usr/bin/bash", args[0]);
        for (size_t i = 1; i < args.size(); i++) {
            ASSERT_EQ(cmdLineArgs.args[i], args[i]);
        }

        ASSERT_EQ(cmdLineArgs.pwd, ep->cmdLineArgs.pwd);
        ASSERT_EQ(cmdLineArgs.home, ep->cmdLineArgs.home);

        ASSERT_TRUE(ep->cbpSearchPaths.empty());
        ASSERT_TRUE(ep->output.empty());
    }
}

TEST_F(CMakerTests, ECHO) {
    CmdLineArgs cmdLineArgs;
    cmdLineArgs.args = {"xecho", "test"};
    cmdLineArgs.pwd = cmaker.getModuleDir();
    cmdLineArgs.home = cmdLineArgs.pwd;
    int r = cmaker.init(cmdLineArgs);
    ASSERT_EQ(0, r);

    const ExecutionPlan *ep = cmaker.getExecutionPlan();
    const auto &env = ep->cmdLineArgs.env;
    ASSERT_EQ(2, env.size());
    ASSERT_TRUE(std::find(env.begin(), env.end(), "E1=1") != env.end());
    ASSERT_TRUE(std::find(env.begin(), env.end(), "E2=2") != env.end());

    ASSERT_EQ("/usr/bin/echo", ep->exePath);

    const auto &args = ep->cmdLineArgs.args;
    ASSERT_EQ(2, args.size());
    ASSERT_EQ("/usr/bin/echo", args[0]);
    ASSERT_EQ("test", args[1]);

    ASSERT_EQ(cmdLineArgs.pwd, ep->cmdLineArgs.pwd);
    ASSERT_EQ(cmdLineArgs.home, ep->cmdLineArgs.home);

    ASSERT_TRUE(ep->cbpSearchPaths.empty());
    ASSERT_TRUE(ep->output.empty());
}

} // namespace gatools
#endif
