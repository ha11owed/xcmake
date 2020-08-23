#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace gatools {

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
};

/// @brief Runs the specified command and patches the cbp file.
class CMaker {
  public:
    CMaker();
    ~CMaker();

    /// @brief get the current execution plan
    const ExecutionPlan *getExecutionPlan() const;

    /// @brief get the directory containing the executable.
    std::string getModuleDir() const;

    /// @brief initialize cmaker
    int init(const CmdLineArgs &cmdLineArgs);

    /// @brief execute the command in the specified working directory.
    int run();

    /// @brief post run
    int patch();

#ifdef CMAKER_WITH_UNIT_TESTS
  public:
#else
  private:
#endif
    struct Impl;
    std::shared_ptr<Impl> _impl;
};

} // namespace gatools
