
#include <stdio.h>
#include <string.h>
#include "dashboard.h"
#include "activity_log.h"
#include "globals.h"
#include "commands.h"
#include "procedures.h"
#include "packets.h"

#define SCREEN_W 400
#define SCREEN_H 240

// Couleurs de l'interface
#define BG_R       10
#define BG_G       13
#define BG_B       17

#define PANEL_R    24
#define PANEL_G    34
#define PANEL_B    46

#define CARD_R     38
#define CARD_G     55
#define CARD_B     72

#define GREEN_R    78
#define GREEN_G    205
#define GREEN_B    139


/*
 * Police bitmap 3x5.
 * Chaque ligne utilise les 3 bits de poids faible.
 */
static const u8 *get_glyph(char c) {
    static const u8 space[5] = {0,0,0,0,0};
    static const u8 letters[26][5] = {
        {2,5,7,5,5}, // A
        {6,5,6,5,6}, // B
        {3,4,4,4,3}, // C
        {6,5,5,5,6}, // D
        {7,4,6,4,7}, // E
        {7,4,6,4,4}, // F
        {3,4,5,5,3}, // G
        {5,5,7,5,5}, // H
        {7,2,2,2,7}, // I
        {1,1,1,5,2}, // J
        {5,5,6,5,5}, // K
        {4,4,4,4,7}, // L
        {5,7,7,5,5}, // M
        {5,7,7,7,5}, // N
        {2,5,5,5,2}, // O
        {6,5,6,4,4}, // P
        {2,5,5,7,3}, // Q
        {6,5,6,5,5}, // R
        {3,4,2,1,6}, // S
        {7,2,2,2,2}, // T
        {5,5,5,5,7}, // U
        {5,5,5,5,2}, // V
        {5,5,7,7,5}, // W
        {5,5,2,5,5}, // X
        {5,5,2,2,2}, // Y
        {7,1,2,4,7}  // Z
    };

    static const u8 digits[10][5] = {
        {7,5,5,5,7}, // 0
        {2,6,2,2,7}, // 1
        {6,1,7,4,7}, // 2
        {6,1,3,1,6}, // 3
        {5,5,7,1,1}, // 4
        {7,4,6,1,6}, // 5
        {3,4,7,5,7}, // 6
        {7,1,2,2,2}, // 7
        {7,5,7,5,7}, // 8
        {7,5,7,1,6}  // 9
    };

    c = (c >= 'a' && c <= 'z') ? c - 32 : c;

    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];

    static const u8 dash[5]  = {0,0,7,0,0};
    static const u8 slash[5] = {1,1,2,4,4};
    static const u8 colon[5] = {0,2,0,2,0};
    static const u8 dot[5]   = {0,0,0,0,2};
    static const u8 percent[5] = {5,1,2,4,5};

    switch (c) {
        case '-': return dash;
        case '/': return slash;
        case ':': return colon;
        case '.': return dot;
        case '%': return percent;
        default: return space;
    }
}

#define HOME_X (-39)
#define HOME_Y 66
#define HOME_Z (-57)

static int selected_player = -1;
static char player_action_status[32] = "";

static int dashboard_player_online(int i) {
    return i >= 0 && i < MAX_PLAYERS && player_data[i].client_fd >= 0 && !(player_data[i].flags & 0x20);
}

static void dashboard_heal_player(PlayerData *player) {
    if (!player || player->client_fd < 0) return;

    player->health = 20;
    player->hunger = 20;
    player->saturation = 20;

    sc_setHealth(player->client_fd, 20, 20, 20);
}

static void draw_round_rect(
    u8 *fb,
    int x, int y,
    int w, int h,
    int radius,
    u8 r, u8 g, u8 b
);

static void draw_char(
    u8 *fb, int x, int y, char c,
    int scale, u8 r, u8 g, u8 b
) {
    const u8 *glyph = get_glyph(c);

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 3; col++) {
            if (glyph[row] & (1 << (2 - col))) {
                draw_round_rect(fb, x + col * scale, y + row * scale, scale, scale, 0, r, g, b);
            }
        }
    }
}

static void draw_text(
    u8 *fb, int x, int y,
    const char *text, int scale,
    u8 r, u8 g, u8 b
) {
    while (*text) {
        draw_char(fb, x, y, *text, scale, r, g, b);
        x += 4 * scale;
        text++;
    }
}

#define BOTTOM_W 320
#define BOTTOM_H 240

static DashboardTab current_tab = TAB_ACTIVITY;

void dashboard_cycle_tab(int direction) {
    if (direction == 0) return;

    int next = (int)current_tab + (direction > 0 ? 1 : -1);

    if (next < 0) {
        next = TAB_COUNT - 1;
    }

    if (next >= TAB_COUNT) {
        next = 0;
    }

    current_tab = (DashboardTab)next;
}

static void bottom_pixel(
    u8 *fb, int x, int y,
    u8 r, u8 g, u8 b
) {
    if (!fb || x < 0 || x >= BOTTOM_W ||y < 0 || y >= BOTTOM_H) return;

    int offset = (x * 240 + (239 - y)) * 3;

    fb[offset] = r;
    fb[offset + 1] = g;
    fb[offset + 2] = b;
}

static void bottom_rect(
    u8 *fb, int x, int y, int w, int h,
    u8 r, u8 g, u8 b
) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            bottom_pixel(fb, px, py, r, g, b);
        }
    }
}

static void bottom_text(
    u8 *fb, int x, int y,
    const char *text,
    u8 r, u8 g, u8 b
) {
    while (*text && x < BOTTOM_W - 3) {
        const u8 *glyph = get_glyph(*text);

        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 3; col++) {
                if (glyph[row] & (1 << (2 - col))) {
                    bottom_pixel(fb, x + col, y + row, r, g, b);
                }
            }
        }
        x += 4;
        text++;
    }
}

static void draw_bottom_tabs(u8 *fb) {
    static const char *labels[TAB_COUNT] = {
        "ACTIVITY",
        "CONSOLE",
        "PLAYERS",
        "LOGS",
        "SETTINGS"
    };

    static const int positions[TAB_COUNT] = {
        12, 70, 128, 186, 236
    };

    static const int widths[TAB_COUNT] = {
        56, 56, 56, 48, 72
    };

    for (int i = 0; i < TAB_COUNT; i++) {
        bool active = (i == current_tab);

        bottom_rect(
            fb,
            positions[i], 24,
            widths[i], 11,
            active ? 38 : 30,
            active ? 55 : 42,
            active ? 72 : 55
        );

        int text_width = strlen(labels[i]) * 4;
        int text_x = positions[i] + (widths[i] - text_width) / 2;

        if (active) {
            bottom_text(fb, text_x, 27, labels[i], 235, 240, 245);
        } else {
            bottom_text(fb, text_x, 27, labels[i], 155, 174, 190);
        }
    }
}

static void put_pixel(
    u8 *fb, int x, int y,
    u8 r, u8 g, u8 b
) {
    if (!fb || x < 0 || x >= SCREEN_W ||
        y < 0 || y >= SCREEN_H) {
        return;
    }

    // Le framebuffer 3DS est stocké sur le côté :
    // 240 pixels de large et 400 pixels de haut.
    int offset = (x * 240 + (239 - y)) * 3;

    fb[offset]     = r;
    fb[offset + 1] = g;
    fb[offset + 2] = b;
}

static void draw_round_rect(
    u8 *fb,
    int x, int y,
    int w, int h,
    int radius,
    u8 r, u8 g, u8 b
) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            int cx = px;
            int cy = py;

            if (px < x + radius) cx = x + radius;
            if (px >= x + w - radius) cx = x + w - radius - 1;

            if (py < y + radius) cy = y + radius;
            if (py >= y + h - radius) cy = y + h - radius - 1;

            int dx = px - cx;
            int dy = py - cy;

            if (dx * dx + dy * dy <= radius * radius) {
                put_pixel(fb, px, py, r, g, b);
            }
        }
    }
}

static void draw_background(u8 *fb) {
    draw_round_rect(fb, 0, 0, SCREEN_W, SCREEN_H, 0, BG_R, BG_G, BG_B);

    // Panneau principal
    draw_round_rect(fb, 8, 8, 384, 224, 8, PANEL_R, PANEL_G, PANEL_B);

    // Cartes statistiques
    draw_round_rect(fb, 16, 55, 178, 43, 5, CARD_R, CARD_G, CARD_B);
    draw_round_rect(fb, 206, 55, 178, 43, 5, CARD_R, CARD_G, CARD_B);
    draw_round_rect(fb, 16, 104, 178, 43, 5, CARD_R, CARD_G, CARD_B);
    draw_round_rect(fb, 206, 104, 178, 43, 5, CARD_R, CARD_G, CARD_B);

    // Panneaux Connection et Performance
    draw_round_rect(fb, 16, 153, 178, 65, 5, CARD_R, CARD_G, CARD_B);
    draw_round_rect(fb, 206, 153, 178, 65, 5, CARD_R, CARD_G, CARD_B);

    // Séparateur de l'en-tête
    for (int x = 16; x < 384; x++) {
        put_pixel(fb, x, 46, 105, 126, 145);
    }

    // Barres d'historique TPS : hauteur provisoire uniforme
    for (int i = 0; i < 12; i++) {
        draw_round_rect(fb, 212 + i * 14, 185, 12, 16, 2, GREEN_R, GREEN_G, GREEN_B);
    }
}

void dashboard_init(void) {
    // No console initialization.
    // Both screens are rendered using framebuffers.
}


void dashboard_render(const DashboardStats *stats) {
    if (!stats) return;

    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    if (!fb) return;

    draw_background(fb);

    const u8 white_r = 235;
    const u8 white_g = 240;
    const u8 white_b = 245;

    const u8 muted_r = 155;
    const u8 muted_g = 174;
    const u8 muted_b = 190;

    // HEADER
    draw_text(fb, 20, 17, "BARECTR", 2, white_r, white_g, white_b);

    draw_text(fb, 20, 35, "SERVER DASHBOARD", 1, muted_r, muted_g, muted_b);

    if (stats->server_ready) {
        draw_text(fb, 330, 20, "ONLINE", 1, GREEN_R, GREEN_G, GREEN_B);
    } else {
        draw_text(fb, 322, 20, "OFFLINE", 1, 235, 180, 80);
    }

    // PLAYERS
    draw_text(fb, 25, 61, "PLAYERS", 1, muted_r, muted_g, muted_b);

    char players[16];
    snprintf(players, sizeof(players), "%d / %d", stats->players, stats->max_players);

    draw_text(fb, 25, 77, players, 2, white_r, white_g, white_b);

    // UPTIME
    draw_text(fb, 215, 61, "UPTIME", 1, muted_r, muted_g, muted_b);

    char uptime[16];

    unsigned hours = stats->uptime_seconds / 3600;
    unsigned minutes = (stats->uptime_seconds % 3600) / 60;
    unsigned seconds = stats->uptime_seconds % 60;

    snprintf(uptime, sizeof(uptime), "%02u:%02u:%02u", hours, minutes, seconds);

    draw_text(fb, 215, 77, uptime, 2, white_r, white_g, white_b);

    // TPS
    draw_text(fb, 25, 110, "TPS", 1, muted_r, muted_g, muted_b);

    char tps_text[16];

    snprintf(tps_text, sizeof(tps_text), "%.1f", stats->tps);

    draw_text(fb, 25, 126, tps_text, 2, white_r, white_g, white_b);

    // RAM
    draw_text(fb, 215, 110, "RAM USED", 1, muted_r, muted_g, muted_b);

    char ram_text[20];

    unsigned ram_tenths_mb = (unsigned)(((uint64_t)stats->ram_used_bytes * 10) / 1048576);

    snprintf(ram_text, sizeof(ram_text), "%u.%u MB", ram_tenths_mb / 10, ram_tenths_mb % 10);

    draw_text(fb, 215, 126, ram_text, 2, white_r, white_g, white_b);

    // CONNECTION
    draw_text(fb, 25, 160, "CONNECTION", 1, white_r, white_g, white_b);

    char ip_text[24];

    snprintf(ip_text, sizeof(ip_text), "IP: %s", stats->ip_address);

    draw_text(fb, 25, 177, ip_text, 1, muted_r, muted_g, muted_b);

    draw_text(fb, 25, 195, "PORT: 25565", 1, muted_r, muted_g, muted_b);

    // PERFORMANCE
    draw_text(fb, 215, 160, "PERFORMANCE", 1, white_r, white_g, white_b);
    draw_text(fb, 215, 177, "TPS HISTORY", 1, muted_r, muted_g, muted_b);
    draw_text(fb, 215, 202, "WAITING", 1, GREEN_R, GREEN_G, GREEN_B);

    // FOOTER
    draw_text(fb, 20, 222, "WI-FI", 1, muted_r, muted_g, muted_b);
    draw_text(fb, 340, 222, "--", 1, muted_r, muted_g, muted_b);
}

void dashboard_render_activity(void) {
    u8 *fb = gfxGetFramebuffer(
        GFX_BOTTOM,
        GFX_LEFT,
        NULL,
        NULL
    );

    if (!fb) return;

    // Background
    bottom_rect(fb, 0, 0, 320, 240, 10, 13, 17);

    // Main panel
    bottom_rect(fb, 6, 6, 308, 228, 24, 34, 46);

    // Header
    bottom_text(fb, 14, 12, "SERVER ACTIVITY", 235, 240, 245);
    bottom_rect(fb, 12, 21, 296, 1, 105, 126, 145);

    // Navigation
    draw_bottom_tabs(fb);

    int count = activity_log_get_count();

    for (int i = 0; i < ACTIVITY_LOG_CAPACITY; i++) {
        int y = 39 + i * 6;

        bottom_rect(
            fb, 12, y - 1, 296, 6,
            (i % 2 == 0) ? 30 : 26,
            (i % 2 == 0) ? 42 : 38,
            (i % 2 == 0) ? 55 : 50
        );

        if (i >= count) continue;

        const ActivityEntry *entry = activity_log_get(i);
        if (!entry) continue;

        // Relative timestamp
        char timestamp[12];

        snprintf(timestamp, sizeof(timestamp), "%02u:%02u", entry->uptime_seconds / 60, entry->uptime_seconds % 60);

        bottom_text(fb, 16, y, timestamp, 155, 174, 190);

        // Event type and color
        const char *type = "INFO";
        u8 r = 155, g = 174, b = 190;

        switch (entry->type) {
            case LOG_INFO:
                type = "INFO";
                break;

            case LOG_JOIN:
                type = "JOIN";
                r = 78; g = 205; b = 139;
                break;

            case LOG_LEAVE:
                type = "LEAVE";
                r = 235; g = 180; b = 80;
                break;

            case LOG_COMMAND:
                type = "CMD";
                r = 100; g = 170; b = 245;
                break;

            case LOG_WARNING:
                type = "WARN";
                r = 245; g = 180; b = 70;
                break;

            case LOG_ERROR:
                type = "ERROR";
                r = 245; g = 90; b = 90;
                break;
        }

        bottom_text(fb, 60, y, type, r, g, b);

        // Event message
        bottom_text(fb, 108, y, entry->message, 220, 225, 230);
    }
}

void dashboard_render_players(void) {
    u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!fb) return;

    bottom_rect(fb, 0, 0, 320, 240, 10, 13, 17);
    bottom_rect(fb, 6, 6, 308, 228, 24, 34, 46);

    if (selected_player >= 0) {
        if (!dashboard_player_online(selected_player)) {
            selected_player = -1;
            player_action_status[0] = '\0';
        } else {
            PlayerData *player = &player_data[selected_player];

            bottom_text(fb, 14, 12, "PLAYER ACTIONS", 235, 240, 245);
            bottom_rect(fb, 12, 21, 296, 1, 105, 126, 145);
            draw_bottom_tabs(fb);

            bottom_text(fb, 20, 49, player->name, 235, 240, 245);

            // TP HOME
            bottom_rect(fb, 12, 65, 296, 30, 35, 85, 65);
            bottom_text(fb, 25, 76, "TP HOME  X:-39 Y:66 Z:-57", 235, 240, 245);

            // HEAL
            bottom_rect(fb, 12, 101, 296, 30, 35, 75, 55);
            bottom_text(fb, 132, 112, "HEAL", 235, 240, 245);

            // KICK
            bottom_rect(fb, 12, 137, 296, 30, 100, 40, 40);
            bottom_text(fb, 137, 148, "KICK", 235, 240, 245);

            // Action result
            bottom_text(fb, 20, 163, player_action_status, 130, 205, 150);

            // Back button
            bottom_rect(fb, 12, 195, 100, 30, 45, 50, 60);
            bottom_text(fb, 43, 206, "BACK", 235, 240, 245);

            return;
        }
    }

    // Count players who have finished loading.
    int online_count = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerData *player = &player_data[i];

        if (player->client_fd >= 0 && !(player->flags & 0x20)) {
            online_count++;
        }
    }

    char header[32];
    snprintf(header, sizeof(header), "PLAYERS %d", online_count);

    bottom_text(fb, 14, 12, header, 235, 240, 245);
    bottom_rect(fb, 12, 21, 296, 1, 105, 126, 145);

    draw_bottom_tabs(fb);

    if (online_count == 0) {
        bottom_text(fb, 88, 112, "NO PLAYERS ONLINE", 155, 174, 190);
        return;
    }

    int row = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerData *player = &player_data[i];

        // Ignore disconnected players and players still loading.
        if (player->client_fd < 0 || (player->flags & 0x20)) {
            continue;
        }

        int y = 42 + row * 46;

        bottom_rect(fb, 12, y, 296, 42, 30, 42, 55);

        // Player name.
        char name[20];
        snprintf(name, sizeof(name), "%.*s", 16, player->name);

        bottom_text(fb, 20, y + 5, name, 235, 240, 245);

        // Health and hunger.
        char stats[32];
        snprintf(stats, sizeof(stats), "HP %u/20  FOOD %u/20", (unsigned)player->health, (unsigned)player->hunger);

        bottom_text(fb, 20, y + 16, stats, 130, 205, 150);

        // Position.
        char position[40];
        snprintf(position, sizeof(position), "POS %d %d %d", (int)player->x, (int)player->y, (int)player->z);

        bottom_text(fb, 20, y + 27, position, 155, 174, 190);

        row++;
    }
}

void dashboard_render_bottom(void) {
    if (current_tab == TAB_ACTIVITY) {
        dashboard_render_activity();
        return;
    }

    if (current_tab == TAB_PLAYERS) {
    dashboard_render_players();
    return;
}

    u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);

    if (!fb) return;

    bottom_rect(fb, 0, 0, 320, 240, 10, 13, 17);
    bottom_rect(fb, 6, 6, 308, 228, 24, 34, 46);

    const char *titles[TAB_COUNT] = {
        "SERVER ACTIVITY",
        "SERVER CONSOLE",
        "CONNECTED PLAYERS",
        "SERVER LOGS",
        "SERVER SETTINGS"
    };

    bottom_text(fb, 14, 12, titles[current_tab], 235, 240, 245);
    bottom_rect(fb, 12, 21, 296, 1, 105, 126, 145);

    draw_bottom_tabs(fb);

    bottom_text(fb, 116, 115, "COMING SOON", 155, 174, 190);
}

void dashboard_handle_touch(int x, int y) {
    if (current_tab != TAB_PLAYERS) return;

    // No player selected: detect a player card.
    if (selected_player < 0) {
        int row = 0;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!dashboard_player_online(i)) continue;

            int card_y = 42 + row * 46;

            if (x >= 12 && x <= 308 &&
                y >= card_y && y <= card_y + 42) {
                selected_player = i;
                player_action_status[0] = '\0';
                return;
            }

            row++;
        }

        return;
    }

    // Back button.
    if (x >= 12 && x <= 112 && y >= 195 && y <= 225) {
        selected_player = -1;
        player_action_status[0] = '\0';
        return;
    }

    // TP HOME button.
    if (x >= 12 && x <= 308 && y >= 75 && y <= 113) {

        if (!dashboard_player_online(selected_player)) {
            selected_player = -1;
            return;
        }

        PlayerData *player = &player_data[selected_player];

        if (command_teleport(player, HOME_X, HOME_Y, HOME_Z) == 0) {
            snprintf(player_action_status, sizeof(player_action_status), "TELEPORTED HOME");
        } else {
            snprintf(player_action_status, sizeof(player_action_status), "TELEPORT FAILED");
        }
    }

    // HEAL
    if (x >= 12 && x <= 308 &&
        y >= 101 && y <= 131) {

        if (!dashboard_player_online(selected_player)) {
            selected_player = -1;
            return;
        }

        dashboard_heal_player(&player_data[selected_player]);

        snprintf(player_action_status, sizeof(player_action_status), "PLAYER HEALED");
        return;
    }

    // KICK
    if (x >= 12 && x <= 308 &&
        y >= 137 && y <= 167) {

        if (!dashboard_player_online(selected_player)) {
            selected_player = -1;
            return;
        }

        PlayerData *player = &player_data[selected_player];

        // Disconnect the selected player cleanly.
        disconnectClient(&player->client_fd, 0);

        // Return to the player list.
        selected_player = -1;
        player_action_status[0] = '\0';

        return;
    }
}
