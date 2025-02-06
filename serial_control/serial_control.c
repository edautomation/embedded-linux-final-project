#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <signal.h>

#define STDIN_FD 0

static inline void terminate_normally(void)
{
    printf("Terminating normally\n");
    exit(EXIT_SUCCESS);
}

static void handle_signal(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        printf("\nGot SIGINT or SIGTERM\n");
        terminate_normally();
    }
}

int main(int argc, char* argv[])
{
    printf("Hello, world!\n");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    while (true)
    {
    }

    return 0;
}