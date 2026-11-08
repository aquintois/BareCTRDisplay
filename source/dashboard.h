
#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool server_ready;
    int players;
    int max_players;
    int view_distance;

    float tps;
    uint32_t uptime_seconds;
    uint32_t ram_used_bytes;
    char ip_address[16];

} DashboardStats;

void dashboard_init(void);
void dashboard_render(const DashboardStats *stats);

#endif
