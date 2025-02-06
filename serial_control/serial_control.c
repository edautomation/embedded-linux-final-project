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

#define MAX_NAME_LENGTH  30
#define MAX_VALUE_LENGTH 20
#define MAX_CMD_LENGTH   MAX_NAME_LENGTH + MAX_VALUE_LENGTH + 1
#define BUFFER_SIZE      (MAX_CMD_LENGTH + 1)

#define DEBUG
#ifdef DEBUG
#define LOG_DEBUG(...) printf(__VA_ARGS__)
#else
#define LOG_DEBUG(...)
#endif

enum command_type_t
{
    COMMAND_INVALID,
    COMMAND_READ,
    COMMAND_WRITE,
};

struct data_t
{
    char name[MAX_NAME_LENGTH];
    char value[MAX_VALUE_LENGTH];
    struct data_t* next;
};

static char* buffer = NULL;
static struct data_t list_root = {0};
static struct data_t* list_head = &list_root;

static inline void cleanup(void)
{
    if (NULL != buffer)
    {
        free(buffer);
    }

    struct data_t* list_item = list_head;
    while (&list_root != list_item)
    {
        struct data_t* item_to_delete = list_item;
        list_item = list_item->next;
        free(item_to_delete);
    }
}

static inline void terminate_normally(void)
{
    LOG_DEBUG("Terminating normally\n");
    cleanup();
    exit(EXIT_SUCCESS);
}

static inline void terminate_with_error(void)
{
    LOG_DEBUG("Terminating because of an error\n");
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

static enum command_type_t validate_and_prepare_input(char* buffer, int length)
{
    if ((MAX_CMD_LENGTH < length) || (length <= 0))
    {
        return COMMAND_INVALID;
    }

    enum command_type_t cmd_type = COMMAND_INVALID;
    if ('\n' != buffer[length - 1])
    {
        LOG_DEBUG("Command too long\n");
    }
    else if ('!' == buffer[0])
    {
        cmd_type = COMMAND_WRITE;
    }
    else if ('?' == buffer[0])
    {
        cmd_type = COMMAND_READ;
    }
    else
    {
        LOG_DEBUG("Invalid start of command\n");
    }

    if (!(COMMAND_INVALID == cmd_type))
    {
        buffer[length - 1] = '\0';  // replace newline with string terminator for string handling function
        LOG_DEBUG("Read command from standard input: \"%s\".\n", buffer);
    }

    return cmd_type;
}

static void add_or_update_value_for_name(char* string, size_t name_length, char* equal_sign, size_t value_length)
{
    char name[MAX_NAME_LENGTH];
    memset((void*)name, 0, MAX_NAME_LENGTH);
    strncpy(name, string, name_length);
    bool is_new = true;
    struct data_t* list_item = list_head;
    do
    {
        if (0 == strncmp(list_item->name, name, name_length))
        {
            LOG_DEBUG("Found name!\n");
            is_new = false;
            break;
        }
        list_item = list_item->next;
    } while (NULL != list_item);

    if (is_new)
    {
        LOG_DEBUG("New name: %s\n", name);
        list_item = malloc(sizeof(struct data_t));
    }

    strncpy(list_item->name, name, name_length);
    strncpy(list_item->value, equal_sign + 1, value_length);
    list_item->name[name_length] = '\0';
    list_item->value[value_length] = '\0';
    LOG_DEBUG("Writing %s = %s\n", list_item->name, list_item->value);
    printf("OK\n");

    if (is_new)
    {
        list_item->next = list_head;
        list_head = list_item;
    }
}

static void handle_write_command(void)
{
    LOG_DEBUG("Write command\n");

    char* write_command = buffer + 1;
    char* equal_sign = strchr(write_command, '=');
    if (NULL == equal_sign)
    {
        LOG_DEBUG("Invalid format, missing =\n");
    }
    else
    {
        size_t name_length = equal_sign - write_command;
        size_t value_length = strlen(write_command) - name_length - 1;
        if (name_length > MAX_NAME_LENGTH || value_length > MAX_VALUE_LENGTH)
        {
            LOG_DEBUG("Invalid format, name or value string too long\n");
        }
        else
        {
            add_or_update_value_for_name(write_command, name_length, equal_sign, value_length);
        }
    }
}

static void handle_read_command(void)
{
    char* name = buffer + 1;
    size_t name_length = strlen(name);
    if (name_length > MAX_NAME_LENGTH)
    {
        LOG_DEBUG("Invalid format, name string too long!\n");
    }
    else
    {
        struct data_t* list_item = list_head;
        do
        {
            if (0 == strcmp(list_item->name, name))
            {
                printf("%s\n", list_item->value);
                break;
            }
            list_item = list_item->next;
        } while (NULL != list_item);

        if (NULL == list_item)
        {
            printf("NO ENTRY\n");
        }
    }
}

int main(int argc, char* argv[])
{
    LOG_DEBUG("Hello, serial control!\n");

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
            LOG_DEBUG("Could not read from standard input.\n");
        }
        else
        {
            enum command_type_t cmd_type = validate_and_prepare_input(buffer, read_length);
            if (COMMAND_INVALID == cmd_type)
            {
                LOG_DEBUG("Invalid command, ignored\n");
            }
            else if (COMMAND_WRITE == cmd_type)
            {
                handle_write_command();
            }
            else if (COMMAND_READ == cmd_type)
            {
                handle_read_command();
            }
            else
            {
                LOG_DEBUG("Unexpected path in code!\n");
                terminate_with_error();
            }
        }
        free(buffer);
    }

    return 0;
}
