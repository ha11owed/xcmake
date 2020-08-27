#pragma once

#include "Config.h"
#include "tinyxml2.h"
#include <deque>

namespace gatools {

using XmlElemPtr = tinyxml2::XMLElement *;
using XmlElemParentPair = std::pair<XmlElemPtr, XmlElemPtr>;

struct CbpPatchContext {
    tinyxml2::XMLDocument inOutXml;
    std::string cbpFilePath;

    std::string projectDir;
    std::string buildDir;
    std::string sdkDir;
    std::vector<std::string> extraAddDirectory;
    std::set<std::string> gccClangFixes;

    std::string virtualFolderPrefix;
    std::string oldSdkPrefix;
    std::string oldVirtualFolderPrefix;
};

enum class PatchResult { Changed, Unchanged, AlreadyPatched, DifferentSDK, Error };

const char *asString(PatchResult value);

std::ostream &operator<<(std::ostream &os, PatchResult in);

bool getAttribute(XmlElemPtr elem, const char *attrName, std::string &outValue);

void addPrefix(XmlElemPtr elem, const char *attrName, const std::string &prefix);

void addPrefixToVirtualFolder(const CbpPatchContext &executionPlan, std::string &value);

void addPrefixToVirtualFolder(const CbpPatchContext &executionPlan, XmlElemPtr elem, const char *attrName);

bool readNote(XmlElemPtr elem, CbpPatchContext &executionPlan);

XmlElemPtr createNote(const CbpPatchContext &executionPlan, XmlElemPtr elem);

PatchResult patchCBP(CbpPatchContext &context, std::string *outModifiedXml = nullptr);

} // namespace gatools
