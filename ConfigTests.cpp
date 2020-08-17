#ifdef CMAKER_WITH_UNIT_TESTS
#include "Config.h"
#include "gtest/gtest.h"

namespace gatools {

class ConfigTests : public ::testing::Test {
  public:
    JConfig createConfig(bool includeStar = true);
};

JConfig ConfigTests::createConfig(bool includeStar) {
    JConfig expected;
    {
        expected.cmdEnvironment = {"E1=1", "E2=2"};
        expected.cmdReplacement = {{"xecho", {"/usr/bin/echo", "${sdkPath}"}}};
        JProject p;
        p.cmdEnvironment = {"E3=3"};
        p.cmdReplacement["xcmake"] = {"${sdkPath}/cmake", "cmake"};
        p.path = "/home/testuser/project0";
        p.sdkPath = "/home/testuser/sdks/v42";
        expected.projects.push_back(p);
        if (includeStar) {
            p.path = "*";
            p.sdkPath = "/home/testuser/sdks/v45";
            expected.projects.push_back(p);
        }
        p.path = "/home/testuser/project2";
        p.sdkPath = "/home/testuser/sdks/v43";
        expected.projects.push_back(p);
    }
    return expected;
}

TEST_F(ConfigTests, JsonSerialization) {
    JConfig expected = createConfig();

    std::string s = serialize(expected);
    JConfig actual = deserialize(s);
    ASSERT_EQ(expected, actual);
}

TEST_F(ConfigTests, Simplify) {
    JConfig config;
    {
        JProject proj;
        proj.path = "/some/path//.";
        proj.sdkPath = "/some/./sdk/";
        proj.buildPaths.insert("/build1");
        proj.buildPaths.insert("/build1/.");
        proj.buildPaths.insert("/build2/");
        proj.buildPaths.insert("/build2/././");
        config.projects.push_back(proj);
    }

    simplify(config);

    JProject &p = config.projects[0];
    ASSERT_EQ("/some/path", p.path);
    ASSERT_EQ("/some/sdk", p.sdkPath);
    ASSERT_EQ(2, p.buildPaths.size());
    ASSERT_TRUE(p.buildPaths.find("/build1") != p.buildPaths.end());
    ASSERT_TRUE(p.buildPaths.find("/build2") != p.buildPaths.end());
}

TEST_F(ConfigTests, SelectProject0) {
    JConfig config = createConfig();
    JProject expectedProject = config.projects[0];
    expectedProject.cmdEnvironment.insert("E1=1");
    expectedProject.cmdEnvironment.insert("E2=2");
    expectedProject.cmdReplacement.insert({"xecho", {"/usr/bin/echo", "/home/testuser/sdks/v42"}});
    expectedProject.cmdReplacement["xcmake"][0] = "/home/testuser/sdks/v42/cmake";

    const std::vector<std::string> buildOrProjPaths{"/home/testuser/project0", "/home/testuser/project0/",
                                                    "/home/testuser/project0/somedir/"};
    for (const std::string &path : buildOrProjPaths) {
        JProject actualProject;
        ASSERT_TRUE(selectProject(config, path, actualProject));
        ASSERT_EQ("/home/testuser/project0", actualProject.path);
        ASSERT_EQ(expectedProject, actualProject);
    }
}

TEST_F(ConfigTests, SelectProjectStar) {
    JConfig config = createConfig();
    JProject expectedProject = config.projects[1];
    expectedProject.cmdEnvironment.insert("E1=1");
    expectedProject.cmdEnvironment.insert("E2=2");
    expectedProject.cmdReplacement.insert({"xecho", {"/usr/bin/echo", "/home/testuser/sdks/v45"}});
    expectedProject.cmdReplacement["xcmake"][0] = "/home/testuser/sdks/v45/cmake";

    JProject actualProject;
    ASSERT_TRUE(selectProject(config, "/home/testuser/projectNotMatching", actualProject));
    ASSERT_EQ("*", actualProject.path);
    ASSERT_EQ(expectedProject, actualProject);
}

TEST_F(ConfigTests, SelectNoProject) {
    JConfig config = createConfig();
    config.projects.clear();

    JProject actualProject;
    ASSERT_FALSE(selectProject(config, "/home/testuser/projectNotMatching", actualProject));
    ASSERT_EQ("", actualProject.path);
    ASSERT_EQ("", actualProject.sdkPath);
}

TEST_F(ConfigTests, UpdateProject2) {
    JConfig expected = createConfig(false);
    expected.projects[1].buildPaths.insert("/home/testuser/buildDir2");
    JConfig actual = createConfig(false);
    actual.projects[0].buildPaths.insert("/home/testuser/buildDir2");

    ASSERT_TRUE(updateProject("/home/testuser/project2", "/home/testuser/buildDir2", actual));
    ASSERT_EQ(expected, actual);
}

TEST_F(ConfigTests, UpdateProjectInexistent) {
    JConfig expected = JConfig();
    JConfig actual = expected;
    ASSERT_FALSE(updateProject("/home/testuser/projectNotMatching", "/home/testuser/buildDir", actual));
    ASSERT_EQ(expected, actual);
}

} // namespace gatools
#endif
