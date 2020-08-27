#include <CMaker.h>

#include <Config.h>
#include <file_system.h>
#include <gtest/gtest.h>

namespace gatools {

static std::string g_cmakerJson;
static std::string g_inputCbp;
static std::string g_expectedCbp;

class CMakerTests : public ::testing::Test {
  public:
    void SetUp() override;

    void createTestDir();
    void createCbpFile();

    CMaker cmaker;

    std::string _tmpDir;
    std::string _projectDir;
    std::string _buildDir;
    std::string _cbpFilePath;
};

void CMakerTests::SetUp() {
    if (g_cmakerJson.empty()) {
        ga::readFile("cmaker.json", g_cmakerJson);
    }
    if (g_inputCbp.empty()) {
        ga::readFile("testproject_input.cbp", g_inputCbp);
    }
    if (g_expectedCbp.empty()) {
        ga::readFile("testproject_output.cbp.xml", g_expectedCbp);
    }

    _tmpDir = "/tmp/xcmake/test";
    _projectDir = ga::combine(_tmpDir, "proj42");
    _buildDir = ga::combine(_tmpDir, "build");
    _cbpFilePath = ga::combine(_buildDir, "proj42.cbp");
}

void CMakerTests::createTestDir() {
    mkdir("/tmp/xcmake/", S_IRWXU);
    mkdir(_tmpDir.c_str(), S_IRWXU);

    std::string cmakerJsonPath = ga::combine(_tmpDir, "cmaker.json");
    remove(cmakerJsonPath.c_str());
    ga::writeFile(cmakerJsonPath, g_cmakerJson);

    mkdir(_projectDir.c_str(), S_IRWXU);
    mkdir(_buildDir.c_str(), S_IRWXU);

    remove(_cbpFilePath.c_str());
}

void CMakerTests::createCbpFile() { ga::writeFile("/tmp/xcmake/test/build/proj42.cbp", g_inputCbp); }

TEST_F(CMakerTests, CMAKE_BASH) {
    createTestDir();

    std::string sdkDir = ga::combine(_tmpDir, "sdks/v42");

    CmdLineArgs cmdLineArgs;
    cmdLineArgs.args = {"xcmake", _projectDir, "'-GCodeBlocks - Unix Makefiles'"};
    cmdLineArgs.pwd = _buildDir;
    cmdLineArgs.home = _tmpDir;
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

        ASSERT_EQ(_buildDir, ep->buildDir);
        ASSERT_EQ(_projectDir, ep->projectDir);
        ASSERT_EQ(sdkDir, ep->sdkDir);
        ASSERT_EQ(2, ep->extraAddDirectory.size());
        ASSERT_EQ(2, ep->gccClangFixes.size());

        ASSERT_EQ(1, ep->cbpSearchPaths.size());
        ASSERT_EQ(_buildDir, ep->cbpSearchPaths[0]);
        ASSERT_EQ(1, ep->output.size());
    }

    // we skip the run and just write the
    ga::writeFile(_buildDir + "/proj42.cbp", g_inputCbp);

    r = cmaker.patch();
    ASSERT_EQ(0, r);
    std::string actualCbp;
    ga::readFile(_cbpFilePath, actualCbp);
    ASSERT_EQ(actualCbp, g_expectedCbp);

    // Execute bash in the build directory
    cmdLineArgs.args = {"xbash"};
    cmdLineArgs.pwd = ga::combine(_buildDir, "some_dir");
    cmdLineArgs.home = _tmpDir;
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

TEST_F(CMakerTests, CMAKE_CP_TO_BUILD) {
    createTestDir();

    std::string sdkDir = ga::combine(_tmpDir, "sdks/v42");

    CmdLineArgs cmdLineArgs;
    cmdLineArgs.args = {"cmakeCPtoBuild", _projectDir, "'-GCodeBlocks - Unix Makefiles'"};
    cmdLineArgs.pwd = _buildDir;
    cmdLineArgs.home = _tmpDir;
    int r = cmaker.init(cmdLineArgs);
    ASSERT_EQ(0, r);
    r = cmaker.run();
    ASSERT_EQ(0, r);
    r = cmaker.patch();
    ASSERT_EQ(0, r);

    std::string actualCbp;
    ga::readFile(_cbpFilePath, actualCbp);
    ASSERT_EQ(actualCbp, g_expectedCbp);
}

TEST_F(CMakerTests, ECHO) {
    CmdLineArgs cmdLineArgs;
    cmdLineArgs.args = {"xecho", "test"};
    cmdLineArgs.pwd = "/home";
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
