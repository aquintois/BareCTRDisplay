#include "activity_log.h"

#include <stdio.h>
#include <string.h>

static ActivityEntry entries[ACTIVITY_LOG_CAPACITY];
static int start_index = 0;
static int entry_count = 0;

void activity_log_init(void) {
    memset(entries, 0, sizeof(entries));
    start_index = 0;
    entry_count = 0;
}

void activity_log_add(
    ActivityType type,
    const char *message,
    unsigned int uptime_seconds
) {
    int index;

    if (entry_count < ACTIVITY_LOG_CAPACITY) {
        index = (start_index + entry_count) % ACTIVITY_LOG_CAPACITY;
        entry_count++;
    } else {
        index = start_index;
        start_index = (start_index + 1) % ACTIVITY_LOG_CAPACITY;
    }

    entries[index].type = type;
    entries[index].uptime_seconds = uptime_seconds;

    snprintf(entries[index].message, sizeof(entries[index].message), "%s", message ? message : "");
}

int activity_log_get_count(void) {
    return entry_count;
}

const ActivityEntry *activity_log_get(int index) {
    if (index < 0 || index >= entry_count) {
        return NULL;
    }

    int physical_index =
        (start_index + index) % ACTIVITY_LOG_CAPACITY;

    return &entries[physical_index];
}
