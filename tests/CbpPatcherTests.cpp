#include <CbpPatcher.h>

#include <file_system.h>
#include <gtest/gtest.h>

namespace gatools {

class CbpPatcherTests : public ::testing::Test {
  public:
    CbpPatcherTests();

    CbpPatchContext context;
};

CbpPatcherTests::CbpPatcherTests() {
    context.cbpFilePath = "/tmp/xcmake/test/build/proj.cbp";
    context.buildDir = "/tmp/xcmake/test/build";
    context.projectDir = "/tmp/xcmake/test/project";
    context.sdkDir = "/tmp/xcmake/test/sdks/v42";
    context.gccClangFixes.insert("-gcc1");
    context.gccClangFixes.insert("-gcc2");
    context.extraAddDirectory.push_back("/extra1");
    context.extraAddDirectory.push_back("/extra2");
}

TEST_F(CbpPatcherTests, CompilerAddDirectory) {
    tinyxml2::XMLDocument doc;
    XmlElemPtr elem = doc.NewElement("Add");
    elem->SetAttribute("directory", "/usr/test/include");
    addPrefix(elem, "directory", "/home/testuser/sdks/v42");

    std::string actual;
    getAttribute(elem, "directory", actual);
    ASSERT_EQ(actual, "/home/testuser/sdks/v42/usr/test/include");
}

TEST_F(CbpPatcherTests, VirtualFoldersNoChange) {
    std::string value = "CMake Files\\;CMake Files\\..\\;CMake Files\\..\\..\\;CMake Files\\..\\..\\somedir\\";
    std::string expected = "CMake Files\\;CMake Files\\..\\;CMake Files\\..\\..\\;CMake Files\\..\\..\\somedir\\";
    addPrefixToVirtualFolder(context, value);
    ASSERT_EQ(expected, value);
}

TEST_F(CbpPatcherTests, VirtualFoldersChange) {
    context.virtualFolderPrefix = "..\\..\\sdk\\v43";
    std::string value = "CMake Files\\..\\..\\..\\..\\usr\\include\\someotherlib";
    std::string expected = "CMake Files\\..\\..\\sdk\\v43\\usr\\include\\someotherlib\\";
    addPrefixToVirtualFolder(context, value);
    ASSERT_EQ(expected, value);
}

TEST_F(CbpPatcherTests, PatchCBPs) {
    std::string expectedTestprojectCbpOutput;
    ASSERT_TRUE(ga::readFile("testproject_output.cbp.xml", expectedTestprojectCbpOutput));

    // Transform the original xml
    context.inOutXml.LoadFile("testproject_input.cbp");
    std::string outXml;
    auto patchResult = patchCBP(context, &outXml);

    ASSERT_EQ(PatchResult::Changed, patchResult);
    ASSERT_EQ(expectedTestprojectCbpOutput, outXml);

    // Already transformed. Nothing will be done
    patchResult = patchCBP(context, &outXml);

    ASSERT_EQ(PatchResult::Unchanged, patchResult);
    ASSERT_EQ("", outXml);
}

} // namespace gatools
