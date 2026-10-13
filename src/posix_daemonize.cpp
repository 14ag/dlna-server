#include "posix_daemonize.h"

#ifdef DLNA_POSIX

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

bool DetachToBackgroundOrPrintReady(const char* readyMessage) {
    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        // cannot set up the readiness handoff stay in the foreground
        // rather than silently fail to print the ready message
        return true;
    }

    pid_t child = fork();
    if (child < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        return true;
    }

    if (child > 0) {
        // parent waits for the childs single byte readiness signal then
        // prints and exits so the shell prompt returns immediately after
        close(pipeFds[1]);
        char ready = 0;
        ssize_t readCount = read(pipeFds[0], &ready, 1);
        close(pipeFds[0]);
        if (readCount == 1 && ready == 1) {
            std::fprintf(stdout, "%s\n", readyMessage);
            std::fflush(stdout);
            std::exit(0);
        }
        // child failed before signaling readiness surface that plainly
        std::fprintf(stderr, "dlna-server: background start failed\n");
        std::exit(1);
    }

    // child process drop the controlling terminal
    close(pipeFds[0]);
    setsid();

    int devNull = open("/dev/null", O_RDWR);
    if (devNull >= 0) {
        dup2(devNull, STDIN_FILENO);
        dup2(devNull, STDOUT_FILENO);
        dup2(devNull, STDERR_FILENO);
        if (devNull > STDERR_FILENO) close(devNull);
    }

    char one = 1;
    ssize_t written = write(pipeFds[1], &one, 1);
    (void)written;
    close(pipeFds[1]);
    return false;
}

#endif // DLNA_POSIX