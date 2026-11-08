#ifndef ACTIVITY_LOG_H
#define ACTIVITY_LOG_H

#define ACTIVITY_LOG_CAPACITY 30
#define ACTIVITY_MESSAGE_MAX 80

typedef enum {
    LOG_INFO,
    LOG_JOIN,
    LOG_LEAVE,
    LOG_COMMAND,
    LOG_WARNING,
    LOG_ERROR
} ActivityType;

typedef struct {
    ActivityType type;
    unsigned int uptime_seconds;
    char message[ACTIVITY_MESSAGE_MAX];
} ActivityEntry;

void activity_log_init(void);
void activity_log_add(
    ActivityType type,
    const char *message,
    unsigned int uptime_seconds
);
int activity_log_get_count(void);
const ActivityEntry *activity_log_get(int index);

unsigned int server_uptime_seconds(void);

#endif