#include "CbpPatcher.h"

#include "file_system.h"
#include "loguru.hpp"

#include <sstream>

namespace gatools {

const char *asString(PatchResult value) {
    switch (value) {
    case PatchResult::Changed:
        return "Changed";
    case PatchResult::Unchanged:
        return "Unchanged";
    case PatchResult::DifferentSDK:
        return "DifferentSDK";
    case PatchResult::Error:
        return "Error";
    }
    return "Unknown PatchResult";
}

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

bool getAttribute(XmlElemPtr elem, const char *attrName, std::string &outValue) {
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

void addPrefix(XmlElemPtr elem, const char *attrName, const std::string &prefix) {
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

void addPrefixToVirtualFolder(const CbpPatchContext &executionPlan, std::string &value) {
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

        // check if the path is inside the source dir
        std::string simpleVirtualPath = ga::combine(executionPlan.buildDir, virtualPath);
        cleanPathSeparators(simpleVirtualPath, '/');
        ga::getSimplePath(simpleVirtualPath, simpleVirtualPath);
        if (simpleVirtualPath.find("/usr/") == 0) {
            // virtual must be put in the SDK
            simpleVirtualPath = ga::combine(executionPlan.virtualFolderPrefix, simpleVirtualPath);
            cleanPathSeparators(simpleVirtualPath, '\\');
            virtualPath = simpleVirtualPath;
        } else {
            ga::getSimplePath(virtualPath, virtualPath);
        }

        if (virtualPath.size() > 0 && !ga::isPathSeparator(virtualPath.back())) {
            virtualPath += "\\";
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

void addPrefixToVirtualFolder(const CbpPatchContext &executionPlan, XmlElemPtr elem, const char *attrName) {
    std::string value;
    if (!getAttribute(elem, attrName, value)) {
        return;
    }

    addPrefixToVirtualFolder(executionPlan, value);

    elem->SetAttribute(attrName, value.c_str());
}

bool readNote(XmlElemPtr elem, CbpPatchContext &executionPlan) {
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

XmlElemPtr createNote(const CbpPatchContext &executionPlan, XmlElemPtr elem) {
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

/// @brief patch the .cbp at the filePath.
PatchResult patchCBP(CbpPatchContext &context, std::string *outModifiedXml) {
    PatchResult patchResult = PatchResult::Error;

    if (outModifiedXml != nullptr) {
        (*outModifiedXml).clear();
    }

    // will contain the relative path from the directory of the filePath to the sdk folder
    std::string virtualFolderPrefix;
    std::string dir = ga::getParent(context.cbpFilePath);
    if (!ga::getRelativePath(dir, context.sdkDir, virtualFolderPrefix)) {
        LOG_F(ERROR, "cannot get relative path: %s => %s", dir.c_str(), context.sdkDir.c_str());
        return patchResult;
    }

    cleanPathSeparators(virtualFolderPrefix, '\\');
    context.virtualFolderPrefix = "..\\" + virtualFolderPrefix;

    bool hasNotes = false;
    bool hasNewNote = false;

    tinyxml2::XMLPrinter printerIn;
    tinyxml2::XMLDocument &inOutXml = context.inOutXml;
    inOutXml.Print(&printerIn);
    std::string original(printerIn.CStr());

    std::deque<XmlElemParentPair> q;
    enqueueWithSiblings(inOutXml.FirstChildElement(), nullptr, q);

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
            addPrefix(curr, "directory", context.sdkDir);
        } else if (name == "Unit") {
            addPrefix(curr, "filename", context.sdkDir);
        } else if (parent == "MakeCommands") {
            static std::set<std::string> makeCommandChildren = {"Build", "CompileFile", "Clean", "DistClean"};
            if (makeCommandChildren.find(name) != makeCommandChildren.end()) {
                /// @todo maybe replace the commands with our own set?
                // addPrefix(curr, "command", in.sdkDir);
            }
        } else if (parent == "Unit" && name == "Option") {
            addPrefixToVirtualFolder(context, curr, "virtualFolder");
        } else if (parent == "Project" && name == "Option") {
            // In the Project section there will be multiple Option children.
            if (readNote(curr, context)) {
                hasNotes = true;
            } else {
                if (!hasNotes) {
                    createNote(context, parentElem);
                    hasNotes = true;
                    hasNewNote = true;
                }
                addPrefixToVirtualFolder(context, curr, "virtualFolders");
            }
        }

        enqueueWithSiblings(curr->FirstChildElement(), curr, q);

        if (hasNewNote) {
            if (context.oldVirtualFolderPrefix == context.virtualFolderPrefix) {
                patchResult = PatchResult::Unchanged;
            } else {
                patchResult = PatchResult::DifferentSDK;
            }
            break;
        }

        /// @todo we should actually store the old values in the note instead of just checking if the note exists.
        if (hasNewNote && name == "Compiler") {
            for (const std::string &addDir : context.extraAddDirectory) {
                XmlElemPtr elem = curr->InsertNewChildElement("Add");
                elem->SetAttribute("directory", addDir.c_str());
                addPrefix(elem, "directory", context.sdkDir);
            }

            // Add the options at the beginning of the Compiler section
            for (const std::string &addOption : context.gccClangFixes) {
                XmlElemPtr elem = curr->InsertNewChildElement("Add");
                elem->SetAttribute("option", addOption.c_str());
                curr->InsertFirstChild(elem);
            }
        }
    }

    tinyxml2::XMLPrinter printerOut;
    inOutXml.Print(&printerOut);
    std::string modified(printerOut.CStr());

    if (original != modified) {
        if (outModifiedXml != nullptr) {
            *outModifiedXml = modified;
        }
        patchResult = PatchResult::Changed;
    } else {
        patchResult = PatchResult::Unchanged;
    }

    return patchResult;
}

} // namespace gatools
