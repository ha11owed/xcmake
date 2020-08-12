#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace gatools {

struct JSharedConfig {
    std::set<std::string> cmdEnvironment;
    std::map<std::string, std::vector<std::string>> cmdReplacement;
    std::vector<std::string> extraAddDirectory;
    std::set<std::string> gccClangFixes;
};

struct JProject : public JSharedConfig {
    std::string path;
    std::string sdkPath;
    std::set<std::string> buildPaths;
};

struct JConfig : public JSharedConfig {
    std::vector<JProject> projects;
};

bool operator==(const JProject &lhs, const JProject &rhs);
bool operator!=(const JProject &lhs, const JProject &rhs);

bool operator==(const JConfig &lhs, const JConfig &rhs);
bool operator!=(const JConfig &lhs, const JConfig &rhs);

std::string serialize(const JConfig &in);
JConfig deserialize(const std::string &in);

void simplify(JConfig &inOut);

bool selectProject(const JConfig &in, const std::string &projectOrBuildDir, JProject &out);

/// @brief will check if the build directories actually exist in the file system when updating.
bool updateProject(const std::string &projectDir, const std::string &buildDir, JConfig &inOut);

} // namespace gatools
