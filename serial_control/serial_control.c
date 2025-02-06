#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#define MAX_CMD_LENGTH 50
#define BUFFER_SIZE    (MAX_CMD_LENGTH + 1)

static char* buffer;

static inline void cleanup(void)
{
    if (NULL != buffer)
    {
        free(buffer);
    }
}

static inline void terminate_normally(void)
{
    printf("Terminating normally\n");
    cleanup();
    exit(EXIT_SUCCESS);
}

static inline void terminate_with_error(void)
{
    printf("Terminating because of an error\n");
    cleanup();
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

static bool validate_and_prepare_input(char* buffer, int length)
{
    if ((MAX_CMD_LENGTH < length) || (0 == length))
    {
        return false;
    }

    bool is_valid = true;
    if ('\n' != buffer[length - 1])
    {
        printf("Command too long\n");
        is_valid = false;
    }
    else if (('!' != buffer[0]) && ('?' != buffer[0]))
    {
        printf("Invalid start of command\n");
        is_valid = false;
    }
    else
    {
        buffer[length - 1] = '\0';  // replace newline with string terminator for string handling functions
        printf("Read command from standard input: \"%s\".\n", buffer);
    }
    return is_valid;
}

int main(int argc, char* argv[])
{
    printf("Hello, serial control!\n");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    while (true)
    {
        buffer = malloc(BUFFER_SIZE * sizeof(char));
        if (NULL == buffer)
        {
            terminate_with_error();
        }

        int read_length = read(STDIN_FILENO, buffer, MAX_CMD_LENGTH);
        if (read_length < 0)
        {
            printf("Could not read from standard input.\n");
        }
        else
        {
            bool is_valid = validate_and_prepare_input(buffer, read_length);
            if (!is_valid)
            {
                printf("Invalid command, ignored\n");
            }
        }
        free(buffer);
    }

    return 0;
}