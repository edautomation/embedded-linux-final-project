#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#define MAX_CMD_LENGTH 50
#define BUFFER_SIZE    (MAX_CMD_LENGTH + 1)

static inline void terminate_normally(void)
{
    printf("Terminating normally\n");
    exit(EXIT_SUCCESS);
}

static inline void terminate_with_error(void)
{
    printf("Terminating because of an error\n");
    exit(EXIT_FAILURE);
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
        char* buffer = malloc(BUFFER_SIZE * sizeof(char));
        if (NULL == buffer)
        {
            terminate_with_error();
        }

        int res = read(STDIN_FILENO, buffer, MAX_CMD_LENGTH);
        if (res < 0)
        {
            printf("Could not read from standard input.\n");
        }
        else
        {
            buffer[res] = '\0';  // for string handling functions
            printf("Read %d bytes from standard input: \"%s\".\n", res, buffer);
        }
    }

    return 0;
}