#include "CMaker.h"

#include "Config.h"
#include "file_system.h"
#include "json.hpp"
#include "loguru.hpp"
#include "tinyxml2.h"
#include "whereami.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <deque>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef CMAKER_WITH_UNIT_TESTS
#include "gtest/gtest.h"
#endif

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
        LOG_STR_ARRAY(header " cbpSearchPaths", (executionPlan).cbpSearchPaths);                                       \
        LOG_STR_ARRAY(header " output", (executionPlan).output);                                                       \
    } while (false)

namespace gatools {

using XmlElemPtr = tinyxml2::XMLElement *;
using XmlElemParentPair = std::pair<XmlElemPtr, XmlElemPtr>;

inline void split(const std::string &input, const std::string &separator, std::vector<std::string> &parts) {
    size_t start = 0;
    size_t end = input.find(separator);
    while (end != std::string::npos) {
        parts.push_back(input.substr(start, end - start));
        start = end + separator.length();
        end = input.find(separator, start);
    }
    if (start < input.size() - 1) {
        parts.push_back(input.substr(start));
    }
}

inline std::string join(const std::vector<std::string> &content, const std::string &separator) {
    std::stringstream ss;
    size_t n = content.size();
    if (n > 0) {
        ss << content[0];
        for (size_t i = 1; i < n; i++) {
            ss << separator;
            ss << content[i];
        }
    }
    return ss.str();
}

inline void cleanPathSeparators(std::string &path, char separator) {
    for (char &c : path) {
        if (ga::isPathSeparator(c)) {
            c = separator;
        }
    }
}

inline std::vector<const char *> vecToRaw(const std::vector<std::string> &in) {
    std::vector<const char *> outRaw;
    outRaw.resize(in.size() + 1);
    for (size_t i = 0; i < in.size(); i++) {
        outRaw[i] = in[i].c_str();
    }
    outRaw[in.size()] = nullptr;
    return outRaw;
}

inline bool getAttribute(XmlElemPtr elem, const char *attrName, std::string &outValue) {
    bool ok = false;
    if (elem != nullptr) {
        const char *value = elem->Attribute(attrName);
        if (value && strlen(value) > 0) {
            outValue = value;
            ok = true;
        }
    }
    return ok;
}

inline void addPrefix(XmlElemPtr elem, const char *attrName, const std::string &prefix) {
    std::string value;
    if (!getAttribute(elem, attrName, value)) {
        return;
    }

    size_t idx = value.find("/usr/");
    if (idx == std::string::npos) {
        return;
    }

    value = prefix + value.substr(idx);
    ga::getSimplePath(value, value);
    elem->SetAttribute(attrName, value.c_str());
}

inline void addPrefixToVirtualFolder(const ExecutionPlan &executionPlan, std::string &value) {
    const std::string DELIMIT = ";";
    std::vector<std::string> parts;
    split(value, DELIMIT, parts);

    const std::string CMakeFiles_BS = "CMake Files\\";
    size_t n = parts.size();
    for (size_t i = 0; i < n; i++) {
        std::string part = parts[i];

        // part must begin with "CMake Files\" otherwise continue (maybe error?)
        if (part.find(CMakeFiles_BS) != 0) {
            continue;
        }

        std::string virtualPath(part.substr(CMakeFiles_BS.size()));
        if (executionPlan.oldVirtualFolderPrefix.size() > 0 &&
            virtualPath.find(executionPlan.oldVirtualFolderPrefix) == 0) {
            // replace the old virtual folder prefix with the new one
            virtualPath = virtualPath.substr(executionPlan.oldVirtualFolderPrefix.size());
            virtualPath = executionPlan.virtualFolderPrefix + virtualPath;
        } else {
            // check if the path is inside the source dir
            std::string simpleVirtualPath = ga::combine(executionPlan.buildDir, part);
            cleanPathSeparators(simpleVirtualPath, '/');
            ga::getSimplePath(simpleVirtualPath, simpleVirtualPath);
            if (simpleVirtualPath.find("/usr/") == 0) {
                // virtual must be put in the SDK
                simpleVirtualPath = ga::combine(executionPlan.virtualFolderPrefix, simpleVirtualPath);
                cleanPathSeparators(simpleVirtualPath, '\\');
                virtualPath = simpleVirtualPath;
            }
        }

        parts[i] = CMakeFiles_BS + virtualPath;
    }

    if (n > 0) {
        value = parts[0];
        for (size_t i = 1; i < n; i++) {
            value += ";";
            value += parts[i];
        }
    }
}

inline void addPrefixToVirtualFolder(const ExecutionPlan &executionPlan, XmlElemPtr elem, const char *attrName) {
    std::string value;
    if (!getAttribute(elem, attrName, value)) {
        return;
    }

    addPrefixToVirtualFolder(executionPlan, value);

    elem->SetAttribute(attrName, value.c_str());
}

inline bool readNote(XmlElemPtr elem, ExecutionPlan &executionPlan) {
    std::string showNotes;
    bool ok = false;
    for (;;) {
        if (!getAttribute(elem, "show_notes", showNotes)) {
            break;
        }
        XmlElemPtr child = elem->FirstChildElement();
        if (!child) {
            break;
        }

        std::string data = child->GetText();
        auto itS = data.find("<![CDATA[");
        auto itE = data.find("]]>");
        // strip the <![CDATA[]]> to keep the content only
        if (itS == 0 && itE == data.size() - 4) {
            data = data.substr(9, data.size() - 12);
        }

        std::vector<std::string> content;
        split(data, "\n", content);
        if (content.size() >= 2) {
            executionPlan.oldSdkPrefix = content[0];
            executionPlan.oldVirtualFolderPrefix = content[1];
        }

        std::vector<std::string> newContent;
        newContent.push_back(executionPlan.sdkDir);
        newContent.push_back(executionPlan.virtualFolderPrefix);
        std::string sNewContent = join(newContent, "\n");
        child->SetText(sNewContent.c_str());

        ok = true;
        break;
    }
    return ok;
}

inline XmlElemPtr createNote(const ExecutionPlan &executionPlan, XmlElemPtr elem) {
    XmlElemPtr option = elem->InsertNewChildElement("Option");
    option->SetAttribute("show_notes", "0");
    XmlElemPtr notes = option->InsertNewChildElement("notes");

    tinyxml2::XMLText *text = notes->InsertNewText("");
    text->SetCData(true);

    std::vector<std::string> newContent;
    newContent.push_back(executionPlan.sdkDir);
    newContent.push_back(executionPlan.virtualFolderPrefix);
    std::string sContent = join(newContent, "\n");
    text->SetValue(sContent.c_str());

    elem->InsertFirstChild(option);
    return option;
}

inline void enqueueWithSiblings(XmlElemPtr elem, XmlElemPtr parent, std::deque<XmlElemParentPair> &q) {
    if (elem == nullptr) {
        return;
    }

    q.push_back(std::make_pair(elem, parent));
    while ((elem = elem->NextSiblingElement()) != nullptr) {
        q.push_back(std::make_pair(elem, parent));
    }
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

struct CMaker::Impl {
    using WriteFileCb = std::function<void(const std::string &filePath, const std::string &content)>;

    enum class PatchResult { Changed, Unchanged, Error };

    const char *asString(PatchResult value) {
        switch (value) {
        case PatchResult::Changed:
            return "Changed";
        case PatchResult::Unchanged:
            return "Unchanged";
        case PatchResult::Error:
            return "Error";
        }
        return "Unknown PatchResult";
    }

    CmdLineArgs cmdLineArgs;
    ExecutionPlan executionPlan;

    WriteFileCb writeFileCb;

    void enqueueWithSiblings(XmlElemPtr elem, XmlElemPtr parent, std::deque<XmlElemParentPair> &q) {
        if (elem == nullptr) {
            return;
        }

        q.push_back(std::make_pair(elem, parent));
        while ((elem = elem->NextSiblingElement()) != nullptr) {
            q.push_back(std::make_pair(elem, parent));
        }
    }

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

    void writeCbp(WriteFileCb cb) { writeFileCb = cb; }

    /// @brief gather the parameters for patching the .cbp files to use a SDK.
    /// @return true if the CBPs should be patched and the parameters have been gathered.
    bool canPatchCBP(const CmdLineArgs &cmdLineArgs, std::string &outProjectDir, std::string &outBuildDir) {
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
                if (updateProject(projectDir, buildDir, config)) {
                    doUpdate = true;
                }
            }
        }

        if (doUpdate) {
            std::string jStr = serialize(config);
            ga::writeFile(selectedConfigFilePath, jStr);
        }

        return !outProject.sdkPath.empty();
    }

    /// @brief patch the .cbp at the filePath.
    PatchResult patchCBP(const std::string &filePath, tinyxml2::XMLDocument &inXml) {
        PatchResult patchResult = PatchResult::Error;

        // will contain the relative path from the directory of the filePath to the sdk folder
        std::string virtualFolderPrefix;
        std::string dir = ga::getParent(filePath);
        if (!ga::getRelativePath(dir, executionPlan.sdkDir, virtualFolderPrefix)) {
            LOG_F(ERROR, "cannot get relative path: %s => %s", dir.c_str(), executionPlan.sdkDir.c_str());
            return patchResult;
        }

        cleanPathSeparators(virtualFolderPrefix, '\\');
        executionPlan.virtualFolderPrefix = "..\\" + virtualFolderPrefix;
        LOG_ExecutionPlan("", executionPlan);

        bool hasNotes = false;

        tinyxml2::XMLPrinter printerIn;
        inXml.Print(&printerIn);
        std::string original(printerIn.CStr());

        std::deque<XmlElemParentPair> q;
        enqueueWithSiblings(inXml.FirstChildElement(), nullptr, q);

        while (!q.empty()) {
            XmlElemParentPair currPair = q.front();
            XmlElemPtr curr = currPair.first;
            XmlElemPtr parentElem = currPair.second;
            q.pop_front();

            std::string parent;
            if (parentElem != nullptr && parentElem->Name() != nullptr) {
                parent = parentElem->Name();
            }

            const char *name_cstr = curr->Name();
            if (name_cstr == nullptr) {
                continue;
            }

            std::string name(name_cstr);

            if (parent == "Compiler" && name == "Add") {
                addPrefix(curr, "directory", executionPlan.sdkDir);
            } else if (name == "Unit") {
                addPrefix(curr, "filename", executionPlan.sdkDir);
            } else if (parent == "MakeCommands") {
                static std::set<std::string> makeCommandChildren = {"Build", "CompileFile", "Clean", "DistClean"};
                if (makeCommandChildren.find(name) != makeCommandChildren.end()) {
                    /// @todo maybe replace the commands with our own set?
                    // addPrefix(curr, "command", in.sdkDir);
                }
            } else if (parent == "Unit" && name == "Option") {
                addPrefixToVirtualFolder(executionPlan, curr, "virtualFolder");
            } else if (parent == "Project" && name == "Option") {
                // In the Project section there will be multiple Option children.
                if (readNote(curr, executionPlan)) {
                    hasNotes = true;
                } else {
                    if (!hasNotes) {
                        createNote(executionPlan, parentElem);
                        hasNotes = true;
                    }
                    addPrefixToVirtualFolder(executionPlan, curr, "virtualFolders");
                }
            }

            enqueueWithSiblings(curr->FirstChildElement(), curr, q);

            if (name == "Compiler") {
                for (const std::string &addDir : executionPlan.extraAddDirectory) {
                    XmlElemPtr elem = curr->InsertNewChildElement("Add");
                    elem->SetAttribute("directory", addDir.c_str());
                    addPrefix(elem, "directory", executionPlan.sdkDir);
                }

                // Add the options at the beginning of the Compiler section
                for (const std::string &addOption : executionPlan.extraAddDirectory) {
                    XmlElemPtr elem = curr->InsertNewChildElement("Add");
                    elem->SetAttribute("option", addOption.c_str());
                    curr->InsertFirstChild(elem);
                }
            }
        }

        tinyxml2::XMLPrinter printerOut;
        inXml.Print(&printerOut);
        std::string modified(printerOut.CStr());

        bool isModified = (original != modified);
        if (isModified) {
            patchResult = PatchResult::Changed;
            if (writeFileCb) {
                writeFileCb(filePath, modified);
            }
        } else {
            patchResult = PatchResult::Unchanged;
        }

        return patchResult;
    }

    /// @brief patch the .cbp files from the build directory.
    void patchCBPs(const std::vector<std::string> &cbpFilePaths) {
        executionPlan.output.clear();

        // Process all CBP files
        for (const std::string &filePath : cbpFilePaths) {
            tinyxml2::XMLDocument inXml;
            tinyxml2::XMLError error = inXml.LoadFile(filePath.c_str());
            if (error != tinyxml2::XML_SUCCESS) {
                LOG_F(ERROR, "%s cannot be loaded", filePath.c_str());
                continue;
            }

            PatchResult patchResult = patchCBP(filePath, inXml);

            LOG_F(INFO, "patchCBPs filePath: %s PatchResult: %s", executionPlan.sdkDir.c_str(), asString(patchResult));
            executionPlan.output.push_back(filePath + " PatchResult: " + asString(patchResult));

            switch (patchResult) {
            case PatchResult::Changed:
                if (!writeFileCb) {
                    std::string outFile = filePath + ".tmp";
                    std::string bakFile = filePath + ".bak";
                    inXml.SaveFile(outFile.c_str());
                    if (!ga::pathExists(bakFile)) {
                        std::rename(filePath.c_str(), bakFile.c_str());
                    }
                    std::rename(outFile.c_str(), filePath.c_str());
                }
                break;
            case PatchResult::Unchanged:
                break;
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

            // Gather all the CBP search paths
            if (patchCbp) {
                executionPlan.cbpSearchPaths.push_back(executionPlan.buildDir);
                executionPlan.output.push_back("All *.cbp in directory " + executionPlan.buildDir + " will be patched");
            }

            LOG_ExecutionPlan("init.ep", executionPlan);
            retCode = 0;
            break;
        }

        return retCode;
    }

    int step2run() {
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
        if (!hasExecutionPlan()) {
            return -1;
        }

        std::vector<std::string> cbpFilePaths;
        ga::DirectorySearch ds;
        ds.includeFiles = true;
        ds.includeDirectories = false;
        ds.maxRecursionLevel = 0;
        ds.allowedExtensions.insert("cbp");
        for (const std::string &searchDir : executionPlan.cbpSearchPaths) {
            ga::findInDirectory(
                searchDir, [&cbpFilePaths](const ga::ChildEntry &entry) { cbpFilePaths.push_back(entry.path); }, ds);
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

void CMaker::writeCbp(WriteFileCb writeFileCb) {
    if (_impl) {
        _impl->writeCbp(writeFileCb);
    }
}

} // namespace gatools

#ifdef CMAKER_WITH_UNIT_TESTS

namespace gatools {

class CMakerHelperTest : public ::testing::Test {
  public:
    CMakerHelperTest() {
        loguru::g_stderr_verbosity = loguru::Verbosity_OFF;

        executionPlan.projectDir = "/home/testuser/project";
        executionPlan.buildDir = "/home/testuser/build-proj";
        executionPlan.sdkDir = "/home/testuser/sdks/v42";
    }

    ExecutionPlan executionPlan;
};

TEST_F(CMakerHelperTest, VirtualFoldersNoChange) {

    std::string value = "CMake Files\\;CMake Files\\..\\;CMake Files\\..\\..\\;CMake Files\\..\\..\\..\\";
    std::string expected = "CMake Files\\;CMake Files\\..\\;CMake Files\\..\\..\\;CMake Files\\..\\..\\..\\";
    addPrefixToVirtualFolder(executionPlan, value);
    ASSERT_EQ(expected, value);
}

TEST_F(CMakerHelperTest, VirtualFoldersChange) {

    executionPlan.virtualFolderPrefix = "..\\..\\sdk\\v43";
    std::string value = "CMake Files\\..\\..\\..\\..\\usr\\include\\someotherlib";
    std::string expected = "CMake Files\\..\\..\\sdk\\v43\\usr\\include\\someotherlib";
    addPrefixToVirtualFolder(executionPlan, value);
    ASSERT_EQ(expected, value);
}

static std::string cmakerJson;

class CMakerTest : public ::testing::Test {
  public:
    CMakerTest()
        : impl(cmaker._impl)
        , executionPlan(impl->executionPlan) {
        loguru::g_stderr_verbosity = loguru::Verbosity_OFF;

        if (cmakerJson.empty()) {
            ga::readFile("cmaker.json", cmakerJson);
        }
    }

    CMaker cmaker;

    // convenient access to the parts of the cmaker struct.
    std::shared_ptr<CMaker::Impl> impl;
    ExecutionPlan &executionPlan;
};

TEST_F(CMakerTest, PatchCBPs) {
    std::vector<std::pair<std::string, std::string>> cbpPathAndContents;
    cmaker.writeCbp([&cbpPathAndContents](const std::string &path, const std::string &content) {
        cbpPathAndContents.push_back(make_pair(path, content));
    });

    std::string expectedTestprojectCbpOutput;
    ASSERT_TRUE(ga::readFile("testproject_output.cbp.xml", expectedTestprojectCbpOutput));

    executionPlan.projectDir = "/home/testuser/project";
    executionPlan.buildDir = "/home/testuser/build-proj";
    executionPlan.sdkDir = "/home/testuser/sdks/v42";

    // Transform the original xml
    tinyxml2::XMLDocument inXml;
    inXml.LoadFile("testproject_input.cbp");
    auto patchResult = impl->patchCBP("/home/testuser/build-proj/testproject_input.cbp", inXml);

    ASSERT_EQ(CMaker::Impl::PatchResult::Changed, patchResult);
    ASSERT_EQ(1, cbpPathAndContents.size());
    ASSERT_EQ(expectedTestprojectCbpOutput, cbpPathAndContents[0].second);

    // Already transformed. Nothing will be done
    patchResult = impl->patchCBP("/home/testuser/build-proj/testproject_input.cbp", inXml);

    ASSERT_EQ(CMaker::Impl::PatchResult::Unchanged, patchResult);
    ASSERT_EQ(1, cbpPathAndContents.size());
}

TEST_F(CMakerTest, CMAKE_BASH) {
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

        ASSERT_EQ(1, ep->cbpSearchPaths.size());
        ASSERT_EQ(buildDir, ep->cbpSearchPaths[0]);
        ASSERT_EQ(1, ep->output.size());
    }

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

TEST_F(CMakerTest, ECHO) {
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
