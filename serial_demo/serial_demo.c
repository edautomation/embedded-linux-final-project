#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define DEBUG
#ifdef DEBUG
#define LOG_DEBUG(...) printf(__VA_ARGS__)
#else
#define LOG_DEBUG(...)
#endif

static inline void cleanup(void)
{
    // TODO
}

static inline void terminate_normally(void)
{
    LOG_DEBUG("Bye bye!\n");
    cleanup();
    exit(EXIT_SUCCESS);
}

static inline void terminate_with_error(void)
{
    LOG_DEBUG("Ouch, got to abort!\n");
    cleanup();
    exit(EXIT_FAILURE);
}

static void handle_signal(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        LOG_DEBUG("\nGot SIGINT or SIGTERM\n");
        terminate_normally();
    }
}

int main(int argc, char* argv[])
{
    LOG_DEBUG("********* Modbus serial demo *********\n");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    LOG_DEBUG(">> Reading 16 registers...\n");

    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Writing 4 registers... \n");

    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Reading 16 registers... \n");

    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Setting address to the 5th register.. \n");

    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Writing 4 register.. \n");

    LOG_DEBUG("Done! \n");

    LOG_DEBUG(">> Reading 16 register.. \n");

    LOG_DEBUG("Done! \n");
    return 0;
}
