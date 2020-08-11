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
    bool overrideFiles = false;
    bool outputToStdout = false;
    std::vector<std::string> cbpFilePaths;
    std::string projectDir;
    std::string buildDir;
    std::string sdkPath;
    std::vector<std::string> extraAddDirectory;
    std::set<std::string> gccClangFixes;

    std::string sdkDir;
    std::string virtualFolderPrefix;
    std::string oldSdkPrefix;
    std::string oldVirtualFolderPrefix;

    std::vector<std::string> logs;
};

/// @brief Runs the specified command and patches the cbp file.
class CMaker {
  public:
    using WriteFileCb = std::function<void(const std::string &filePath, const std::string &content)>;

    CMaker();
    ~CMaker();

    /// @brief get the current execution plan
    const ExecutionPlan *getExecutionPlan() const;

    /// @brief get the directory containing the executable.
    std::string getModuleDir() const;

    /// @brief callback for writing the CBP file.
    /// It will override the default action of overriding the original file.
    void writeCbp(WriteFileCb writeFileCb);

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
