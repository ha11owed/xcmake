#include "Config.h"

#include "file_system.h"
#include "json.hpp"
#include <iomanip>

namespace gatools {

inline void readJValue(const nlohmann::json &jObj, const std::string &key, std::string &out) {
    if (jObj.contains(key) && jObj[key].is_string()) {
        jObj[key].get_to(out);
    }
}

inline void readJValue(const nlohmann::json &jObj, const std::string &key, bool &out) {
    if (jObj.contains(key) && jObj[key].is_boolean()) {
        jObj[key].get_to(out);
    }
}

inline void readJValue(const nlohmann::json &jObj, const std::string &key, std::set<std::string> &out) {
    if (jObj.contains(key) && jObj[key].is_array()) {
        for (const auto &jElem : jObj[key]) {
            if (jElem.is_string()) {
                out.insert(jElem.get<std::string>());
            }
        }
    }
}

inline void readJValue(const nlohmann::json &jObj, const std::string &key, std::vector<std::string> &out) {
    if (jObj.contains(key) && jObj[key].is_array()) {
        for (const auto &jElem : jObj[key]) {
            if (jElem.is_string()) {
                out.push_back(jElem.get<std::string>());
            }
        }
    }
}

inline void readJValue(const nlohmann::json &jObj, const std::string &key,
                       std::map<std::string, std::vector<std::string>> &out) {
    if (jObj.contains(key) && jObj[key].is_object()) {
        for (const auto &kv : jObj[key].items()) {
            const auto &jVal = kv.value();
            std::vector<std::string> values;
            if (jVal.is_array()) {
                for (const auto &jElem : jVal) {
                    if (jElem.is_string()) {
                        values.push_back(jElem.get<std::string>());
                    }
                }
            }
            out[kv.key()] = values;
        }
    }
}

inline void readJSharedConfig(const nlohmann::json &jObj, JSharedConfig &out) {
    readJValue(jObj, "cmdEnvironment", out.cmdEnvironment);
    readJValue(jObj, "cmdReplacement", out.cmdReplacement);
    readJValue(jObj, "gccClangFixes", out.gccClangFixes);
    readJValue(jObj, "extraAddDirectory", out.extraAddDirectory);
}

inline void readJProject(const nlohmann::json &jObj, JProject &out) {
    readJSharedConfig(jObj, out);
    readJValue(jObj, "path", out.path);
    readJValue(jObj, "sdkPath", out.sdkPath);
    readJValue(jObj, "buildPaths", out.buildPaths);
}

inline void readJConfig(const nlohmann::json &jObj, JConfig &out) {
    readJSharedConfig(jObj, out);
    if (jObj.contains("projects")) {
        const nlohmann::json &jProjects = jObj["projects"];
        if (jProjects.is_array()) {
            for (const nlohmann::json &jProject : jProjects) {
                JProject proj;
                readJProject(jProject, proj);
                out.projects.push_back(proj);
            }
        }
    }
}

inline void writeJSharedConfig(const JSharedConfig &in, nlohmann::json &jOut) {
    jOut["cmdEnvironment"] = in.cmdEnvironment;
    jOut["cmdReplacement"] = in.cmdReplacement;
    jOut["gccClangFixes"] = in.gccClangFixes;
    jOut["extraAddDirectory"] = in.extraAddDirectory;
}

inline void writeJProject(const JProject &in, nlohmann::json &jOut) {
    writeJSharedConfig(in, jOut);
    jOut["path"] = in.path;
    jOut["sdkPath"] = in.sdkPath;
    jOut["buildPaths"] = in.buildPaths;
}

inline void writeJConfig(const JConfig &in, nlohmann::json &jOut) {
    writeJSharedConfig(in, jOut);

    nlohmann::json outProjects = nlohmann::json::array();
    for (const JProject &jProject : in.projects) {
        nlohmann::json outProj;
        writeJProject(jProject, outProj);
        outProjects.push_back(outProj);
    }
    jOut["projects"] = outProjects;
}

inline bool isJSharedConfigEqual(const JSharedConfig &lhs, const JSharedConfig &rhs) {
    return lhs.cmdEnvironment == rhs.cmdEnvironment && lhs.cmdReplacement == rhs.cmdReplacement;
}

bool operator==(const JProject &lhs, const JProject &rhs) {
    if (&lhs == &rhs) {
        return true;
    }
    if (isJSharedConfigEqual(lhs, rhs)) {
        return lhs.path == rhs.path && lhs.sdkPath == rhs.sdkPath && lhs.buildPaths == rhs.buildPaths;
    }
    return false;
}

bool operator==(const JConfig &lhs, const JConfig &rhs) {
    if (&lhs == &rhs) {
        return true;
    }
    if (isJSharedConfigEqual(lhs, rhs)) {
        return lhs.projects == rhs.projects;
    }
    return false;
}

bool operator!=(const JProject &lhs, const JProject &rhs) { return !(lhs == rhs); }

bool operator!=(const JConfig &lhs, const JConfig &rhs) { return !(lhs == rhs); }

std::ostream &operator<<(std::ostream &os, const JProject &in) {
    nlohmann::json jObj;
    writeJProject(in, jObj);
    std::string str = jObj.dump();
    os << str;
    return os;
}

std::ostream &operator<<(std::ostream &os, const JConfig &in) {
    nlohmann::json jObj;
    writeJConfig(in, jObj);
    std::string str = jObj.dump();
    os << str;
    return os;
}

std::string serialize(const JConfig &in) {
    nlohmann::json jObj;
    writeJConfig(in, jObj);
    std::string str = jObj.dump(2);
    return str;
}

JConfig deserialize(const std::string &in) {
    nlohmann::json jObj = nlohmann::json::parse(in);
    JConfig config;
    readJConfig(jObj, config);
    return config;
}

void simplify(JConfig &inOut) {
    for (JProject &proj : inOut.projects) {
        ga::getSimplePath(proj.path, proj.path);
        ga::getSimplePath(proj.sdkPath, proj.sdkPath);

        std::set<std::string> buildPaths;
        for (const std::string &path : proj.buildPaths) {
            std::string spath;
            ga::getSimplePath(path, spath);
            buildPaths.insert(spath);
        }
        proj.buildPaths = buildPaths;
    }
}

inline void replaceAll(const std::string &from, const std::string &to, std::string &inOutStr) {
    if (from.empty()) {
        return;
    }
    size_t start_pos = 0;
    while ((start_pos = inOutStr.find(from, start_pos)) != std::string::npos) {
        inOutStr.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

inline const JProject *findByPredicate(const JConfig &in, std::function<bool(const JProject &)> predicate) {
    for (const JProject &proj : in.projects) {
        if (proj.path != "*" && predicate(proj)) {
            return &proj;
        }
    }
    for (const JProject &proj : in.projects) {
        if (proj.path == "*" && predicate(proj)) {
            return &proj;
        }
    }
    return nullptr;
}

bool selectProject(const JConfig &in, const std::string &projectOrBuildDir, JProject &out) {
    const JProject *selectedProj = findByPredicate(in, [&projectOrBuildDir](const JProject &proj) {
        if ((projectOrBuildDir.find(proj.path) == 0) ||
            (proj.buildPaths.find(projectOrBuildDir) != proj.buildPaths.end())) {
            return true;
        }
        for (const std::string &buildPath : proj.buildPaths) {
            if (projectOrBuildDir.find(buildPath) == 0) {
                return true;
            }
        }
        return proj.path == "*";
    });

    if (selectedProj != nullptr) {
        out = *selectedProj;

        const std::string sdkDirWithS(out.sdkPath + "/");

        for (const std::string &val : in.gccClangFixes) {
            out.gccClangFixes.insert(val);
        }

        std::vector<std::string> dirs = in.extraAddDirectory;
        for (const std::string &dir : out.extraAddDirectory) {
            dirs.push_back(dir);
        }
        for (std::string &dir : dirs) {
            replaceAll("${sdkPath}", sdkDirWithS, dir);
            ga::getSimplePath(dir, dir);
        }
        out.extraAddDirectory = dirs;

        for (const std::string &env : in.cmdEnvironment) {
            auto it = out.cmdEnvironment.find(env);
            if (it == out.cmdEnvironment.end()) {
                out.cmdEnvironment.insert(env);
            }
        }

        for (const auto &kv : in.cmdReplacement) {
            auto it = out.cmdReplacement.find(kv.first);
            if (it == out.cmdReplacement.end()) {
                out.cmdReplacement[kv.first] = kv.second;
            }
        }

        std::map<std::string, std::vector<std::string>> smallKeyCmdReplacement;
        for (auto it = out.cmdReplacement.begin(); it != out.cmdReplacement.end(); it++) {
            const std::string &key = it->first;
            std::vector<std::string> &values = it->second;

            for (std::string &val : values) {
                replaceAll("${sdkPath}", sdkDirWithS, val);
                ga::getSimplePath(val, val);
            }

            std::string smallKey = ga::getFilename(key);
            if (out.cmdReplacement.find(smallKey) == out.cmdReplacement.end()) {
                smallKeyCmdReplacement[smallKey] = values;
            }
        }
        out.cmdReplacement.insert(smallKeyCmdReplacement.begin(), smallKeyCmdReplacement.end());
    }

    return (selectedProj != nullptr);
}

bool updateProject(const std::string &projectDir, const std::string &buildDir, JConfig &inOut) {
    const JProject *constSelectedProj =
        findByPredicate(inOut, [&projectDir](const JProject &proj) { return (projectDir.find(proj.path) == 0); });

    bool updated = false;
    if (constSelectedProj != nullptr) {
        JProject *selectedProj = const_cast<JProject *>(constSelectedProj);

        auto it = selectedProj->buildPaths.find(buildDir);
        if (it == selectedProj->buildPaths.end()) {
            selectedProj->buildPaths.insert(buildDir);
            updated = true;
        }

        if (updated) {
            for (JProject &proj : inOut.projects) {
                std::set<std::string> toDelete;
                for (const std::string &path : proj.buildPaths) {
                    if (selectedProj == &proj) {
                        if (path == buildDir) {
                            // we will not remove the current path that we inserted
                            continue;
                        }
                    } else {
                        if (path == buildDir) {
                            toDelete.insert(path);
                            continue;
                        }
                    }

                    if (!ga::pathExists(path)) {
                        toDelete.insert(path);
                    }
                }

                for (const std::string &path : toDelete) {
                    proj.buildPaths.erase(path);
                }
            }
        }
    }

    return updated;
}

inline nlohmann::json to_json(const CmdLineArgs &in) {
    nlohmann::json jObj;
    jObj["args"] = in.args;
    jObj["env"] = in.env;
    jObj["pwd"] = in.pwd;
    jObj["home"] = in.home;
    return jObj;
}

inline nlohmann::json to_json(const ExecutionPlan &in) {
    nlohmann::json jObj;
    jObj["exePath"] = in.exePath;
    jObj["cmd"] = to_json(in.cmdLineArgs);

    jObj["configFilePath"] = in.configFilePath;
    jObj["cbpSearchPaths"] = in.cbpSearchPaths;
    jObj["projectDir"] = in.projectDir;
    jObj["buildDir"] = in.buildDir;
    jObj["sdkDir"] = in.sdkDir;

    jObj["extraAddDirectory"] = in.extraAddDirectory;
    jObj["gccClangFixes"] = in.gccClangFixes;
    jObj["output"] = in.output;
    jObj["log"] = in.log;
    return jObj;
}

std::ostream &operator<<(std::ostream &os, const CmdLineArgs &in) {
    os << to_json(in);
    return os;
}

std::ostream &operator<<(std::ostream &os, const ExecutionPlan &in) {
    os << std::setw(2) << to_json(in);
    return os;
}

std::string serialize(const ExecutionPlan &in) {
    nlohmann::json jObj(to_json(in));
    return jObj.dump(2);
}

std::string serialize(const ExecutionPlan *in) {
    if (in == nullptr) {
        return "";
    }
    return serialize(*in);
}

} // namespace gatools
