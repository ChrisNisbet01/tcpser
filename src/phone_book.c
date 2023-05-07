#include <stdio.h>
#include <string.h>
#include "phone_book.h"
#include "debug.h"

typedef struct phone_book_st
{
    char from[PH_ENTRY_SIZE];
    char to[PH_ENTRY_SIZE];
} phone_book_st;

static phone_book_st phone_book[PH_BOOK_SIZE];
static size_t size;

int pb_init(void)
{
    size = 0;
    return 0;
}

int pb_add(char * from, char * to)
{
    LOG_ENTER();
    if (size < PH_BOOK_SIZE && from != NULL && strlen(from) > 0 && to != NULL && strlen(to) > 0)
    {
        // should really trim spaces.
        strncpy(phone_book[size].from, from, sizeof(phone_book[size].from));
        phone_book[size].from[sizeof(phone_book[size].from) - 1] = '\0';
        strncpy(phone_book[size].to, to, sizeof(phone_book[size].to));
        phone_book[size].to[sizeof(phone_book[size].to) - 1] = '\0';
        size++;
        LOG_EXIT();
        return 0;
    }
    LOG_EXIT();
    return -1;
}

char const *
pb_search(char const * number)
{
    char const * address = number;
    LOG_ENTER();

    for (size_t i = 0; i < size; i++)
    {
        LOG(LOG_INFO, "Searching entry %zu of %zu", i, size);
        if (strcmp(phone_book[i].from, number) == 0)
        {
            LOG(LOG_INFO, "Found a match for '%s': '%s'", number, phone_book[i].to);
            address = phone_book[i].to;
            break;
        }
    }

    LOG_EXIT();
    return address;
}
