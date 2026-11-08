
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

typedef enum {
    TAB_ACTIVITY,
    TAB_CONSOLE,
    TAB_PLAYERS,
    TAB_LOGS,
    TAB_SETTINGS,
    TAB_COUNT
} DashboardTab;

void dashboard_init(void);
void dashboard_render(const DashboardStats *stats);
void dashboard_render_activity(void);
void dashboard_cycle_tab(int direction);
void dashboard_render_bottom(void);

#endif
