#include "CMaker.h"

#include "CbpPatcher.h"
#include "file_system.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <deque>
#include <fstream>
#include <sstream>
#include <thread>

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
    searches.emplace_back(std::make_pair(executionPlan.projectDir, 1));
    searches.emplace_back(std::make_pair(executionPlan.buildDir, 1));
    searches.emplace_back(std::make_pair(executionPlan.cmdLineArgs.home, 1));

    for (const auto &kv : searches) {
        std::string searchDir = kv.first;
        if (searchDir.empty()) {
            continue;
        }

        for (int i = 0; i < kv.second; i++) {
            std::string filePath = ga::combine(searchDir, CMaker::CONFIG_FILENAME);

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
        }
    }

    return patchCbp;
}

#define LOG_F(x)                                                                                                       \
    do {                                                                                                               \
        std::stringstream ss;                                                                                          \
        ss << x;                                                                                                       \
        executionPlan.log.push_back(ss.str());                                                                         \
    } while (false)

#define OUT_F(x)                                                                                                       \
    do {                                                                                                               \
        std::stringstream ss;                                                                                          \
        ss << x;                                                                                                       \
        executionPlan.output.push_back(ss.str());                                                                      \
        executionPlan.log.push_back(ss.str());                                                                         \
    } while (false)

const std::string CMaker::CONFIG_FILENAME = "xcmake.json";

struct CMaker::Impl {
    CmdLineArgs cmdLineArgs;
    ExecutionPlan executionPlan;
    JConfig defaultJConfiguration;

    /// @brief gather the parameters for patching the .cbp files to use a SDK.
    /// @return true if the CBPs should be patched and the parameters have been gathered.
    bool readConfiguration(const std::string &projectDir, const std::string &buildDir, JProject &outProject) {
        LOG_F("preparePatchCBPs");
        outProject = JProject();

        executionPlan.buildDir = cmdLineArgs.pwd;
        std::vector<std::string> configFilePaths = getConfigFilePaths(executionPlan);
        for (const std::string &configFilePath : configFilePaths) {
            LOG_F("configFilePath: " << configFilePath);
        }

        if (configFilePaths.empty() && defaultJConfiguration != JConfig()) {
            std::string defaultConfigFilePath = ga::combine(executionPlan.cmdLineArgs.home, CMaker::CONFIG_FILENAME);
            ga::writeFile(defaultConfigFilePath, serialize(defaultJConfiguration));
            configFilePaths.push_back(defaultConfigFilePath);

            LOG_F("writing default configuration to: " << defaultConfigFilePath);
        }

        JConfig config;
        std::string selectedConfigFilePath;
        for (const std::string &configFilePath : configFilePaths) {
            std::string jStr;
            if (!ga::readFile(configFilePath, jStr)) {
                LOG_F(configFilePath << " could not be read");
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
                LOG_F("Selected project: " << outProject.path << ", sdk: " << outProject.sdkPath);
                if (updateProject(projectDir, buildDir, config)) {
                    LOG_F("Update project: " << projectDir << " with buildDir: " << buildDir);
                    doUpdate = true;
                }
            }
        }

        if (doUpdate) {
            std::string jStr = serialize(config);
            LOG_F("Write " << jStr << " to: " << selectedConfigFilePath);
            ga::writeFile(selectedConfigFilePath, jStr);
        }

        executionPlan.configFilePath = selectedConfigFilePath;
        return !outProject.sdkPath.empty();
    }

    /// @brief patch the .cbp files from the build directory.
    void patchCBPs(const std::vector<std::string> &cbpFilePaths) {
        // Process all CBP files
        for (const std::string &filePath : cbpFilePaths) {
            CbpPatchContext context;
            context.cbpFilePath = filePath;
            context.projectDir = executionPlan.projectDir;
            context.buildDir = executionPlan.buildDir;
            context.sdkDir = executionPlan.sdkDir;
            context.extraAddDirectory = executionPlan.extraAddDirectory;
            context.gccClangFixes = executionPlan.gccClangFixes;

            tinyxml2::XMLError error = context.inOutXml.LoadFile(filePath.c_str());
            if (error != tinyxml2::XML_SUCCESS) {
                LOG_F(filePath << " cannot be loaded");
                continue;
            }

            std::string modified;
            PatchResult patchResult = patchCBP(context, &modified);

            LOG_F(filePath + " PatchResult: " + asString(patchResult));

            switch (patchResult) {
            case PatchResult::Changed: {
                std::string bakFile = filePath + ".bak";
                if (!ga::pathExists(bakFile)) {
                    int bk = std::rename(filePath.c_str(), bakFile.c_str());
                    LOG_F("backup: " << bakFile << " (rename=" << bk << ")");
                }
                bool ok = ga::writeFile(filePath, modified);
                LOG_F("writeFile: " << filePath << " (ok=" << ok << ")");
            } break;
            default:
                break;
            }
        }

        if (!cbpFilePaths.empty()) {
            OUT_F("SDK:    " << executionPlan.sdkDir);
            OUT_F("Config: " << executionPlan.configFilePath);
            OUT_F("Finished patching...\n");
        }
    }

    bool hasExecutionPlan() const { return !executionPlan.exePath.empty() && !executionPlan.cmdLineArgs.args.empty(); }

    int step1init(const CmdLineArgs &cmdLineArgs) {
        // Log the input parameters
        LOG_F("step1 init: " << cmdLineArgs);

        this->cmdLineArgs = cmdLineArgs;
        executionPlan = ExecutionPlan();
        executionPlan.cmdLineArgs = cmdLineArgs;

        bool patchCbp = canPatchCBP(cmdLineArgs, executionPlan.projectDir, executionPlan.buildDir);
        JProject project;
        bool hasConfig = readConfiguration(executionPlan.projectDir, executionPlan.buildDir, project);
        LOG_F("init patchCbp: " << patchCbp << " hasConfig: " << hasConfig);

        int retCode = -1;
        for (;;) {
            if (cmdLineArgs.args.size() == 0) {
                LOG_F("init empty args");
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
                LOG_F("cmdReplacement for: " << cmdLineArgs.args[0] << " does not exist");
                break;
            }

            const std::vector<std::string> &replCmd = replIt->second;

            executionPlan.exePath = replCmd[0];
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
                OUT_F("All *.cbp in " << executionPlan.buildDir << " will use " << executionPlan.sdkDir);
            } else if (ga::getFilename(executionPlan.cmdLineArgs.args[0]).find("cmake") != std::string::npos) {
                OUT_F("Running xcmake...");
            }

            LOG_F("executionPlan: " << executionPlan);
            retCode = 0;
            break;
        }

        return retCode;
    }

    int step2run() {
        executionPlan.output.clear();
        int retCode = -1;
        if (!hasExecutionPlan()) {
            LOG_F("no execution plan");
            return retCode;
        }

        LOG_F("execute: " << executionPlan.exePath);

        fflush(stdout);
        fflush(stderr);

        pid_t pid = fork();
        switch (pid) {
        case -1:
            LOG_F("cannot fork");
            break;
        case 0:
            // Child process
            {
                std::vector<const char *> cmdRaw = vecToRaw(executionPlan.cmdLineArgs.args);
                std::vector<const char *> envRaw = vecToRaw(executionPlan.cmdLineArgs.env);

                retCode = execvpe(executionPlan.exePath.c_str(), const_cast<char *const *>(cmdRaw.data()),
                                  const_cast<char *const *>(envRaw.data()));
                exit(retCode);
            }
            break;
        default:
            // Parent process
            {
                int status;
                int r = waitpid(pid, &status, 0);
                if (r == pid) {
                    retCode = 0;
                }

                LOG_F("waitpid(" << pid << ") returned " << r << ". retCode: " << retCode);
            }
            break;
        }

        fflush(stdout);
        fflush(stderr);

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

void CMaker::setDefaultConfig(const JConfig &config) {
    if (_impl) {
        _impl->defaultJConfiguration = config;
    }
}

const ExecutionPlan *CMaker::getExecutionPlan() const {
    const ExecutionPlan *r = nullptr;
    if (_impl) {
        r = &(_impl->executionPlan);
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
