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

std::ostream &operator<<(std::ostream &os, const JProject &in);
std::ostream &operator<<(std::ostream &os, const JConfig &in);

std::string serialize(const JConfig &in);
JConfig deserialize(const std::string &in);

void simplify(JConfig &inOut);

bool selectProject(const JConfig &in, const std::string &projectOrBuildDir, JProject &out);

/// @brief will check if the build directories actually exist in the file system when updating.
bool updateProject(const std::string &projectDir, const std::string &buildDir, JConfig &inOut);

struct CmdLineArgs {
    std::vector<std::string> args;
    std::vector<std::string> env;
    std::string home;
    std::string pwd;
};

struct ExecutionPlan {
    std::string exePath;
    CmdLineArgs cmdLineArgs;

    std::string configFilePath;
    std::vector<std::string> cbpSearchPaths;
    std::string projectDir;
    std::string buildDir;
    std::string sdkDir;
    std::vector<std::string> extraAddDirectory;
    std::set<std::string> gccClangFixes;

    std::vector<std::string> output;
    std::vector<std::string> log;
};

std::ostream &operator<<(std::ostream &os, const CmdLineArgs &in);
std::ostream &operator<<(std::ostream &os, const ExecutionPlan &in);

std::string serialize(const ExecutionPlan &in);
std::string serialize(const ExecutionPlan *in);

} // namespace gatools
