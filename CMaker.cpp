#include "CMaker.h"

#include "CbpPatcher.h"
#include "file_system.h"
#include "loguru.hpp"
#include "whereami.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <deque>
#include <fstream>
#include <sstream>
#include <thread>

#define LOG_STR(header, v) LOG_F(INFO, header " %s", (v).c_str())
#define LOG_BOOL(header, v) LOG_F(INFO, header " %s", (v) ? "true" : "false")
#define LOG_STR_ARRAY(header, v)                                                                                       \
    do {                                                                                                               \
        for (size_t i = 0; i < (v).size(); i++) {                                                                      \
            LOG_F(INFO, header "[%lu]: %s", i, (v)[i].c_str());                                                        \
        }                                                                                                              \
    } while (false)
#define LOG_CmdLineArgs(header, cmdLineArgs)                                                                           \
    do {                                                                                                               \
        LOG_STR_ARRAY(header " arg", (cmdLineArgs).args);                                                              \
        LOG_STR_ARRAY(header " env", (cmdLineArgs).env);                                                               \
        LOG_STR(header " pwd: ", (cmdLineArgs).pwd);                                                                   \
        LOG_STR(header " home:", (cmdLineArgs).home);                                                                  \
    } while (false)

#define LOG_ExecutionPlan(header, executionPlan)                                                                       \
    do {                                                                                                               \
        LOG_STR(header " exePath: ", (executionPlan).exePath);                                                         \
        LOG_CmdLineArgs(header " cmdLineArgs", (executionPlan).cmdLineArgs);                                           \
                                                                                                                       \
        LOG_STR(header " configFilePath: ", (executionPlan).configFilePath);                                           \
        LOG_STR_ARRAY(header " cbpSearchPaths", (executionPlan).cbpSearchPaths);                                       \
        LOG_STR(header " projectDir: ", (executionPlan).projectDir);                                                   \
        LOG_STR(header " buildDir: ", (executionPlan).buildDir);                                                       \
        LOG_STR(header " sdkDir: ", (executionPlan).sdkDir);                                                           \
                                                                                                                       \
        LOG_STR_ARRAY(header " extraAddDirectory", (executionPlan).extraAddDirectory);                                 \
        LOG_STR_ARRAY(header " output", (executionPlan).output);                                                       \
    } while (false)

namespace gatools {

inline std::vector<const char *> vecToRaw(const std::vector<std::string> &in) {
    std::vector<const char *> outRaw;
    outRaw.resize(in.size() + 1);
    for (size_t i = 0; i < in.size(); i++) {
        outRaw[i] = in[i].c_str();
    }
    outRaw[in.size()] = nullptr;
    return outRaw;
}

/// @brief get a vector of the config files found in the order of search priority
std::vector<std::string> getConfigFilePaths(const ExecutionPlan &executionPlan) {
    std::vector<std::string> configFilePaths;
    std::set<std::string> configFilePathSet;

    std::vector<std::pair<std::string, int>> searches;
    searches.emplace_back(std::make_pair(executionPlan.cmdLineArgs.home, 1));
    searches.emplace_back(std::make_pair(executionPlan.projectDir, 3));
    searches.emplace_back(std::make_pair(executionPlan.buildDir, 3));

    for (const auto &kv : searches) {
        std::string searchDir = kv.first;
        if (searchDir.empty()) {
            continue;
        }

        for (int i = 0; i < kv.second; i++) {
            std::string filePath = ga::combine(searchDir, "cmaker.json");

            auto it = configFilePathSet.find(filePath);
            if (it == configFilePathSet.end()) {
                if (ga::pathExists(filePath)) {
                    configFilePaths.push_back(filePath);
                }
            }

            searchDir = ga::getParent(searchDir);
        }
    }

    return configFilePaths;
}

/// @brief gather the parameters for patching the .cbp files to use a SDK.
/// @return true if the CBPs should be patched and the parameters have been gathered.
inline bool canPatchCBP(const CmdLineArgs &cmdLineArgs, std::string &outProjectDir, std::string &outBuildDir) {
    LOG_F(INFO, "canPatchCBP");
    outProjectDir.clear();
    outBuildDir.clear();

    bool patchCbp = false;

    if (cmdLineArgs.args.size() >= 2) {
        patchCbp = (ga::getFilename(cmdLineArgs.args[0]).find("make") != std::string::npos) &&
                   ga::pathExists(cmdLineArgs.args[1]) &&
                   (ga::pathExists(ga::combine(cmdLineArgs.pwd, "CMakeCache.txt")) ||
                    cmdLineArgs.pwd.find("build") != std::string::npos);
        if (patchCbp) {
            outProjectDir = cmdLineArgs.args[1];
            outBuildDir = cmdLineArgs.pwd;
            ga::getSimplePath(outProjectDir, outProjectDir);
            ga::getSimplePath(outBuildDir, outBuildDir);
            LOG_F(INFO, "projectDir: %s. buildDir: %s. patchCbp: %d", outProjectDir.c_str(), outBuildDir.c_str(),
                  patchCbp);
        } else {
            LOG_F(INFO, "args[1]: %s. patchCbp: %d", cmdLineArgs.args[1].c_str(), patchCbp);
        }
    } else {
        LOG_F(INFO, "args.size() = %lu. patchCbp: %d", cmdLineArgs.args.size(), patchCbp);
    }

    return patchCbp;
}

struct CMaker::Impl {
    CmdLineArgs cmdLineArgs;
    ExecutionPlan executionPlan;

    std::string getModuleDir() const {
        char buffer[1024 * 64];
        int dirLen;
        int n = wai_getModulePath(buffer, sizeof(buffer), &dirLen);

        std::string dir;
        if (n > 0 && dirLen > 0) {
            dir.append(buffer, static_cast<size_t>(dirLen));
        }
        return dir;
    }

    /// @brief gather the parameters for patching the .cbp files to use a SDK.
    /// @return true if the CBPs should be patched and the parameters have been gathered.
    bool readConfiguration(const std::string &projectDir, const std::string &buildDir, JProject &outProject) {
        LOG_F(INFO, "preparePatchCBPs");
        outProject = JProject();

        executionPlan.buildDir = cmdLineArgs.pwd;
        std::vector<std::string> configFilePaths = getConfigFilePaths(executionPlan);
        for (const std::string &configFilePath : configFilePaths) {
            LOG_F(INFO, "configFilePath: %s", configFilePath.c_str());
        }

        JConfig config;
        std::string selectedConfigFilePath;
        for (const std::string &configFilePath : configFilePaths) {
            std::string jStr;
            if (!ga::readFile(configFilePath, jStr)) {
                LOG_F(ERROR, "%s could not be read", configFilePath.c_str());
                continue;
            }

            config = deserialize(jStr);
            simplify(config);
            selectedConfigFilePath = configFilePath;
            break;
        }

        bool doUpdate = false;
        if (!config.projects.empty()) {
            std::string projectOrBuildDir = projectDir;
            if (projectOrBuildDir.empty()) {
                projectOrBuildDir = buildDir;
            }
            if (selectProject(config, projectOrBuildDir, outProject)) {
                LOG_F(INFO, "Selected project: %s, sdk: %s", outProject.path.c_str(), outProject.sdkPath.c_str());
                if (updateProject(projectDir, buildDir, config)) {
                    LOG_F(INFO, "Update project: %s with buildDir: %s", projectDir.c_str(), buildDir.c_str());
                    doUpdate = true;
                }
            }
        }

        if (doUpdate) {
            std::string jStr = serialize(config);
            LOG_F(INFO, "Write %s to: %s", jStr.c_str(), selectedConfigFilePath.c_str());
            ga::writeFile(selectedConfigFilePath, jStr);
        }

        return !outProject.sdkPath.empty();
    }

    /// @brief patch the .cbp files from the build directory.
    void patchCBPs(const std::vector<std::string> &cbpFilePaths) {
        // Process all CBP files
        for (const std::string &filePath : cbpFilePaths) {
            CbpPatchContext context;
            context.cbpFilePath = filePath;
            context.buildDir = executionPlan.buildDir;
            context.sdkDir = executionPlan.sdkDir;
            context.extraAddDirectory = executionPlan.extraAddDirectory;
            context.gccClangFixes = executionPlan.gccClangFixes;

            tinyxml2::XMLError error = context.inOutXml.LoadFile(filePath.c_str());
            if (error != tinyxml2::XML_SUCCESS) {
                LOG_F(ERROR, "%s cannot be loaded", filePath.c_str());
                continue;
            }

            PatchResult patchResult = patchCBP(context);

            LOG_F(INFO, "patchCBPs filePath: %s PatchResult: %s", filePath.c_str(), asString(patchResult));
            executionPlan.output.push_back(filePath + " PatchResult: " + asString(patchResult));

            switch (patchResult) {
            case PatchResult::Changed: {
                std::string outFile = filePath + ".tmp";
                std::string bakFile = filePath + ".bak";
                context.inOutXml.SaveFile(outFile.c_str());
                if (!ga::pathExists(bakFile)) {
                    std::rename(filePath.c_str(), bakFile.c_str());
                }
                std::rename(outFile.c_str(), filePath.c_str());
            } break;
            case PatchResult::Unchanged:
            case PatchResult::DifferentSDK:
            case PatchResult::Error:
                break;
            }
        }

        if (!cbpFilePaths.empty()) {
            LOG_F(INFO, "patchCBPs SDK:    %s", executionPlan.sdkDir.c_str());
            LOG_F(INFO, "patchCBPs config: %s", executionPlan.configFilePath.c_str());
            executionPlan.output.push_back("SDK:    " + executionPlan.sdkDir);
            executionPlan.output.push_back("Config: " + executionPlan.configFilePath);
            executionPlan.output.push_back("Finished patching...\n");
        }
    }

    bool hasExecutionPlan() const { return !executionPlan.exePath.empty() && !executionPlan.cmdLineArgs.args.empty(); }

    int step1init(const CmdLineArgs &cmdLineArgs) {
        // Log the input parameters
        LOG_CmdLineArgs("init", cmdLineArgs);

        this->cmdLineArgs = cmdLineArgs;
        executionPlan = ExecutionPlan();

        bool patchCbp = canPatchCBP(cmdLineArgs, executionPlan.projectDir, executionPlan.buildDir);
        JProject project;
        bool hasConfig = readConfiguration(executionPlan.projectDir, executionPlan.buildDir, project);
        LOG_F(INFO, "init patchCbp: %d", patchCbp);
        LOG_F(INFO, "init hasConfig: %d", hasConfig);

        int retCode = -1;
        for (;;) {
            if (cmdLineArgs.args.size() == 0) {
                LOG_F(ERROR, "init empty args");
                break;
            }
            if (!hasConfig) {
                break;
            }

            auto replIt = project.cmdReplacement.find(cmdLineArgs.args[0]);
            if (replIt == project.cmdReplacement.end()) {
                replIt = project.cmdReplacement.find(ga::getFilename(cmdLineArgs.args[0]));
            }
            if (replIt == project.cmdReplacement.end()) {
                LOG_F(ERROR, "init cmdReplacement for: %s does not exist", cmdLineArgs.args[0].c_str());
                break;
            }

            const std::vector<std::string> &replCmd = replIt->second;

            executionPlan.exePath = replCmd[0];
            executionPlan.cmdLineArgs = cmdLineArgs;
            std::vector<std::string> &args = executionPlan.cmdLineArgs.args;
            args = cmdLineArgs.args;
            for (size_t i = 0; (i + 1) < replCmd.size() && i < args.size(); i++) {
                args[i] = replCmd[i + 1];
            }

            std::vector<std::string> &env = executionPlan.cmdLineArgs.env;
            env = cmdLineArgs.env;
            for (const std::string &v : project.cmdEnvironment) {
                env.push_back(v);
            }

            executionPlan.sdkDir = project.sdkPath;
            executionPlan.gccClangFixes = project.gccClangFixes;
            executionPlan.extraAddDirectory = project.extraAddDirectory;

            // Gather all the CBP search paths and
            // output a message when running cmake to prevent qtcreator from stating the cmake server.
            if (patchCbp) {
                executionPlan.cbpSearchPaths.push_back(executionPlan.buildDir);
                executionPlan.output.push_back("All *.cbp in " + executionPlan.buildDir + " will use " +
                                               executionPlan.sdkDir);
            } else if (executionPlan.cmdLineArgs.args[0].find("cmake") != std::string::npos) {
                executionPlan.output.push_back("Running xcmake...");
            }

            LOG_ExecutionPlan("init.ep", executionPlan);
            retCode = 0;
            break;
        }

        return retCode;
    }

    int step2run() {
        executionPlan.output.clear();
        int retCode = -1;
        if (!hasExecutionPlan()) {
            executionPlan.output.push_back("no execution plan");
            return retCode;
        }

        executionPlan.output.push_back("execute: " + executionPlan.exePath);

        pid_t oldppid = getppid();
        pid_t pid = fork();
        switch (pid) {
        case -1:
            executionPlan.output.push_back("cannot fork");
            break;
        case 0:
            // Child process
            {
                auto tp1 = std::chrono::steady_clock::now();
                while (true) {
                    pid_t ppid = getppid();
                    if (ppid == oldppid) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        executionPlan.output.push_back("getppid matches the parent before the fork" +
                                                       std::to_string(ppid));
                        retCode = 0;
                        break;
                    }

                    auto tp2 = std::chrono::steady_clock::now();
                    auto delta = std::chrono::duration_cast<std::chrono::seconds>(tp2 - tp1);
                    if (delta > std::chrono::seconds(600)) {
                        executionPlan.output.push_back("timeout after " + std::to_string(delta.count()) + " seconds");
                        retCode = -3;
                        break;
                    }
                }
            }
            break;
        default:
            // Parent process
            {
                std::vector<const char *> cmdRaw = vecToRaw(executionPlan.cmdLineArgs.args);
                std::vector<const char *> envRaw = vecToRaw(executionPlan.cmdLineArgs.env);

                retCode = execvpe(executionPlan.exePath.c_str(), const_cast<char *const *>(cmdRaw.data()),
                                  const_cast<char *const *>(envRaw.data()));
            }
            break;
        }
        return retCode;
    }

    int step3patch() {
        executionPlan.output.clear();
        if (!hasExecutionPlan()) {
            return -1;
        }

        std::vector<std::string> cbpFilePaths;
        ga::DirectorySearch ds;
        ds.includeFiles = true;
        ds.includeDirectories = false;
        ds.maxRecursionLevel = 0;
        for (const std::string &searchDir : executionPlan.cbpSearchPaths) {
            ga::findInDirectory(
                searchDir,
                [&cbpFilePaths](const ga::ChildEntry &entry) {
                    const char *ext = ga::getFileExtension(entry.path);
                    if ((ext != nullptr) &&              //
                        (std::tolower(ext[0]) == 'c') && // extension exists
                        (std::tolower(ext[1]) == 'b') && // and is "cbp"
                        (std::tolower(ext[2]) == 'p') && //
                        (ext[3] == '\0')) {
                        cbpFilePaths.push_back(entry.path);
                    }
                },
                ds);
        }

        patchCBPs(cbpFilePaths);
        return 0;
    }
};

CMaker::CMaker()
    : _impl(std::make_shared<Impl>()) {}

CMaker::~CMaker() {}

const ExecutionPlan *CMaker::getExecutionPlan() const {
    const ExecutionPlan *r = nullptr;
    if (_impl) {
        r = &(_impl->executionPlan);
    }
    return r;
}

std::string CMaker::getModuleDir() const {
    std::string r;
    if (_impl) {
        r = _impl->getModuleDir();
    }
    return r;
}

int CMaker::init(const CmdLineArgs &cmdLineArgs) {
    int r = -1;
    if (_impl) {
        r = _impl->step1init(cmdLineArgs);
    }
    return r;
}

int CMaker::run() {
    int r = -1;
    if (_impl) {
        r = _impl->step2run();
    }
    return r;
}

int CMaker::patch() {
    int r = -1;
    if (_impl) {
        r = _impl->step3patch();
    }
    return r;
}

} // namespace gatools
