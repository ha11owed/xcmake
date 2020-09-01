#pragma once

#include "Config.h"
#include <functional>
#include <memory>

namespace gatools {

/// @brief Runs the specified command and patches the cbp file.
class CMaker {
  public:
    CMaker();
    ~CMaker();

    /// @brief set the default configuration to use.
    /// If no configuration file exists this configuration will be written in the home directory.
    void setDefaultConfig(const JConfig &config);

    /// @brief get the current execution plan
    const ExecutionPlan *getExecutionPlan() const;

    /// @brief initialize cmaker
    int init(const CmdLineArgs &cmdLineArgs);

    /// @brief execute the command in the specified working directory.
    int run();

    /// @brief post run
    int patch();

  public:
    static const std::string CONFIG_FILENAME;

  private:
    struct Impl;
    std::shared_ptr<Impl> _impl;
};

} // namespace gatools
