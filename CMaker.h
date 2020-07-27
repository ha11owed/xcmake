#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gatools {

/// @brief Runs the specified command and patches the cbp file.
class CMaker {
  public:
    using WriteFileCb = std::function<void(const std::string &filePath, const std::string &content)>;

    CMaker();
    ~CMaker();

    /// @brief get the directory containing the executable.
    std::string getModuleDir() const;

    /// @brief callback for writing the CBP file.
    /// It will override the default action of overriding the original file.
    void writeCbp(WriteFileCb writeFileCb);

    /// @brief initialize cmaker
    int init(const std::vector<std::string> &args, const std::vector<std::string> &env, const std::string &home = "",
             const std::string &pwd = "");

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
