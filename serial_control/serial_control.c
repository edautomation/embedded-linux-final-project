#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_CMD_LENGTH 50
#define BUFFER_SIZE    (MAX_CMD_LENGTH + 1)
#define FILENAME       "/var/tmp/serialcontrol.txt"

enum command_type_t
{
    COMMAND_INVALID,
    COMMAND_READ,
    COMMAND_WRITE,
};

static char* buffer = NULL;

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

static enum command_type_t validate_and_prepare_input(char* buffer, int length)
{
    if ((MAX_CMD_LENGTH < length) || (length <= 0))
    {
        return COMMAND_INVALID;
    }

    enum command_type_t cmd_type = COMMAND_INVALID;
    if ('\n' != buffer[length - 1])
    {
        printf("Command too long\n");
    }
    else if ('!' == buffer[0])
    {
        cmd_type = COMMAND_WRITE;
    }
    else if ('?' == buffer[0])
    {
        cmd_type == COMMAND_READ;
    }
    else
    {
        printf("Invalid start of command\n");
    }

    if (!(COMMAND_INVALID == cmd_type))
    {
        buffer[length - 1] = '\0';  // replace newline with string terminator for string handling functions
        printf("Read command from standard input: \"%s\".\n", buffer);
    }

    return cmd_type;
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
            enum command_type_t cmd_type = validate_and_prepare_input(buffer, read_length);
            if (COMMAND_INVALID == cmd_type)
            {
                printf("Invalid command, ignored\n");
            }
            else
            {
                // TODO : handle read or write command
            }
        }
        free(buffer);
    }

    return 0;
}