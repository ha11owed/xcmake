// Defines the entry point for the console application (either run unit tests or the app)

#ifdef CMAKER_WITH_UNIT_TESTS
#include "gtest/gtest.h"

int RunAllTests(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

int main(int argc, char **argv) {
    // The program just runs all the tests.
    return RunAllTests(argc, argv);
}

#else

#include "CMaker.h"
#include "file_system.h"
#include "loguru.hpp"
#include <pwd.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace gatools;

inline void initlog(int argc, char **argv) {
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;

    time_t rawtime;
    struct tm *timeinfo;
    char buffer[80];

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H%M%S", timeinfo);

    std::string logFilePath("/tmp/xmake/");
    if (argc >= 1) {
        std::string fileName = ga::getFilename(argv[0]);
        if (!fileName.empty()) {
            logFilePath += fileName;
            logFilePath += "_";
        }
    }
    logFilePath += buffer;
    logFilePath += "_";
    logFilePath += std::to_string(getpid());
    logFilePath += ".log";
    loguru::add_file(logFilePath.c_str(), loguru::Append, loguru::Verbosity_MAX);
}

inline void printOutput(const CMaker &cmaker) {
    const ExecutionPlan *ep = cmaker.getExecutionPlan();
    for (const std::string &line : ep->output) {
        printf("%s\n", line.c_str());
    }
}

int main(int argc, char **argv) {
    int result = -1;
    for (;;) {
        initlog(argc, argv);

        // CMD
        CmdLineArgs cmdLineArgs;
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
        CMaker cmaker;
        if (argc == 0) {
            printf("0  =   %s\n", argv[0]);
            printf("pwd=   %s\n", cmdLineArgs.pwd.c_str());
            printf("home=  %s\n", cmdLineArgs.home.c_str());
            printf("module=%s\n", cmaker.getModuleDir().c_str());
            break;
        }
        // cmd = {"cmaker", "/home/alin/projects/cpp-httplib/", "'-GCodeBlocks - Unix Makefiles'"};
        // pwd = "/home/alin/projects/build-cpp-httplib-Desktop-Debug/";

        result = cmaker.init(cmdLineArgs);
        printOutput(cmaker);
        if (!result) {
            break;
        }

        // Run the original command. Stop logging to avoid issues with fork.
        loguru::shutdown();
        result = cmaker.run();

        // If we are here we are in the child. Continue logging.
        initlog(argc, argv);
        if (!result) {
            break;
        }

        result = cmaker.patch();
        printOutput(cmaker);
        break;
    }

    return result;
}

#endif
