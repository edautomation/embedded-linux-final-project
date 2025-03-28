#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ht.h"

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

struct modbus_reg_t
{
    char name[MAX_NAME_LENGTH];
    uint16_t addr;
};

struct data_t
{
    uint16_t* addr;
    struct data_t* next;
};

static struct data_t list_root = {0};
static struct data_t* list_head = &list_root;

static char* buffer = NULL;
static ht* hash_table = NULL;

static inline void cleanup(void)
{
    if (NULL != buffer)
    {
        free(buffer);
    }

    if (NULL != hash_table)
    {
        ht_destroy(hash_table);
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

static enum command_type_t validate_and_prepare_input(char* input, int length)
{
    if ((MAX_CMD_LENGTH < length) || (length <= 0))
    {
        return COMMAND_INVALID;
    }

    enum command_type_t cmd_type = COMMAND_INVALID;
    if ('\n' != input[length - 1])
    {
        LOG_DEBUG("Command too long\n");
    }
    else if ('!' == input[0])
    {
        cmd_type = COMMAND_WRITE;
    }
    else if ('?' == input[0])
    {
        cmd_type = COMMAND_READ;
    }
    else
    {
        LOG_DEBUG("Invalid start of command\n");
    }

    if (!(COMMAND_INVALID == cmd_type))
    {
        input[length - 1] = '\0';  // replace newline with string terminator for string handling function
        LOG_DEBUG("Read command from standard input: \"%s\".\n", input);
    }

    return cmd_type;
}

static void handle_write_command(void)
{
    LOG_DEBUG("Write command\n");

    char* write_command = buffer + 1;  // ignore the '!' from now on.

    char name[MAX_NAME_LENGTH];
    uint32_t value = 0;
    int n_matches = sscanf(write_command, "%30[^=]=%u", name, &value);
    if (2 == n_matches)
    {
        if (value > UINT16_MAX)
        {
            LOG_DEBUG("Invalid value (bigger than UINT16_MAX)\n");
        }
        else
        {
            struct data_t* hash_table_entry = ht_get(hash_table, name);
            if (NULL == hash_table_entry)
            {
                LOG_DEBUG("Could not find \"%s\" in mapping!\n", name);
            }
            else
            {
                LOG_DEBUG("Writing %u to register \"%s\" at address %u\n", value, name, hash_table_entry->addr);
                // TODO: write to driver
            }
        }
    }
    else
    {
        LOG_DEBUG("Invalid format!\n");
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
        struct data_t* hash_table_entry = ht_get(hash_table, name);
        if (NULL == hash_table_entry)
        {
            LOG_DEBUG("Could not find \"%s\" in mapping!\n", name);
        }
        else
        {
            LOG_DEBUG("Reading register \"%s\" at address %u\n", name, hash_table_entry->addr);
            // TODO: read from driver
        }
    }
}

static void read_map_file(const char* filename)
{
    hash_table = ht_create();
    if (NULL == hash_table)
    {
        LOG_DEBUG("Out of memory - Could not create hash table\n");
        terminate_with_error();
    }
    else
    {
        LOG_DEBUG("Hash table created\n");
    }

    FILE* map_file = fopen(filename, "r");
    if (NULL == map_file)
    {
        LOG_DEBUG("Could not open mapping file. Abort.\n");
        terminate_with_error();
    }
    else
    {
        LOG_DEBUG("File open\n");
    }

    char* line_buf = NULL;
    size_t buf_len = 0;
    ssize_t len = 0;
    size_t line_nr = 0;
    do
    {
        line_nr++;
        len = getline(&line_buf, &buf_len, map_file);
        if (len < 0)
        {
            LOG_DEBUG("Could not read line or finished reading file\n");
            break;
        }
        else if (len > (MAX_NAME_LENGTH + MAX_VALUE_LENGTH))
        {
            LOG_DEBUG("Line too long: %u\n", len);
            LOG_DEBUG("Invalid line in map file: l.%u\n", line_nr);
            LOG_DEBUG("Line too long: %u\n", len);
            free(line_buf);
            fclose(map_file);
            terminate_with_error();
        }
        else
        {
            LOG_DEBUG("Got line: %s\n", line_buf);
            char name[MAX_NAME_LENGTH];

            struct data_t* list_item = malloc(sizeof(struct data_t));
            list_item->next = list_head;
            list_head = list_item;

            int n_matches = sscanf(line_buf, "%30[^,],%u", name, &list_item->addr);
            if (2 == n_matches)
            {
                LOG_DEBUG("Line has a valid format\n");
                if (NULL == ht_set(hash_table, name, list_item))
                {
                    LOG_DEBUG("Out of memory - Could not add entry to hash table\n");
                    free(line_buf);
                    fclose(map_file);
                    terminate_with_error();
                }
                else
                {
                    LOG_DEBUG("Added mapping: %s, %u\n", name, list_item->addr);
                }
            }
            else
            {
                LOG_DEBUG("Invalid format in line: %s!\n", line_buf);
                free(line_buf);
                fclose(map_file);
                terminate_with_error();
            }
        }
    } while (len > 0);
    free(line_buf);
    fclose(map_file);
}

int main(int argc, char* argv[])
{
    LOG_DEBUG("Hello, serial control!\n");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (argc < 2)
    {
        LOG_DEBUG("Please specify a file with the modbus address mapping.\n");
        LOG_DEBUG("Usage : serial_control path/to/your/file.txt\n");
        return EXIT_SUCCESS;
    }

    read_map_file(argv[1]);

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
