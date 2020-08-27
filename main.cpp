#include "CMaker.h"
#include "file_system.h"
#include <pwd.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace gatools;

inline void printOutput(const CMaker &cmaker) {
    const ExecutionPlan *ep = cmaker.getExecutionPlan();
    for (const std::string &line : ep->output) {
        printf("%s\n", line.c_str());
    }
}

int main(int argc, char **argv) {
    int result = -1;
    CmdLineArgs cmdLineArgs;
    CMaker cmaker;

    for (;;) {
        // CMD
        cmdLineArgs.args.resize(argc);
        for (int i = 0; i < argc; i++) {
            cmdLineArgs.args[i] = argv[i];
        }

        // ENV
        for (int i = 0; environ[i]; i++) {
            cmdLineArgs.env.push_back(environ[i]);
        }

        // Home dir
        {
            passwd *mypasswd = getpwuid(getuid());
            if (mypasswd && mypasswd->pw_dir) {
                cmdLineArgs.home = mypasswd->pw_dir;
            }
        }

        // PWD
        {
            char buff[FILENAME_MAX];
            const char *p = getcwd(buff, FILENAME_MAX);
            if (p) {
                cmdLineArgs.pwd = p;
            }
        }

        // Initialize
        result = cmaker.init(cmdLineArgs);
        printOutput(cmaker);
        if (result != 0) {
            printf("Initialization failed with %d\n", result);
            break;
        }

        // Run the original command.
        result = cmaker.run();
        printOutput(cmaker);
        // If we are here we are in the child. Continue logging.
        if (result != 0) {
            printf("Run failed with %d\n", result);
            break;
        }

        result = cmaker.patch();
        printOutput(cmaker);
        break;
    }

    if (cmdLineArgs.args.size() > 1 && ga::pathExists(cmdLineArgs.args[1]) &&
        ga::getFilename(cmdLineArgs.args[0]).find("cmake") != std::string::npos) {
        std::string sEP = serialize(cmaker.getExecutionPlan());
        if (!sEP.empty()) {
            ga::writeFile("/tmp/xcmake.executionplan", sEP);
        }
    }

    return result;
}
