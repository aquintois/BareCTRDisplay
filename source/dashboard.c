
#include <stdio.h>
#include <string.h>
#include "dashboard.h"
#include "activity_log.h"
#include "globals.h"
#include "commands.h"
#include "procedures.h"
#include "packets.h"
#include "runtime.h"
#include "chunkstream.h"
#include "worldgen.h"
#include "registries.h"

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
        "BITMAP",
        "LOGS",
        "SETTINGS"
    };

    const int start_x = 12;
    const int tab_width = 47;
    const int spacing = 2;

    for (int i = 0; i < TAB_COUNT; i++) {
        bool active = (i == current_tab);
        int x = start_x + i * (tab_width + spacing);

        bottom_rect(
            fb, x, 24, tab_width, 11,
            active ? 38 : 30,
            active ? 55 : 42,
            active ? 72 : 55
        );

        int text_width = strlen(labels[i]) * 4;
        int text_x = x + (tab_width - text_width) / 2;

        bottom_text(
            fb, text_x, 27, labels[i],
            active ? 235 : 155,
            active ? 240 : 174,
            active ? 245 : 190
        );
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

#define MAP_X       16
#define MAP_Y       43
#define MAP_W       288
#define MAP_H       137
#define MAP_RANGE   64

#define MAP_COLS    144
#define MAP_ROWS    68

#define MAP_RECENTER_THRESHOLD 48


static int bitmap_center_x = 0;
static int bitmap_center_z = 0;
static bool bitmap_cache_valid = false;

static uint8_t bitmap_cache[MAP_COLS * MAP_ROWS];

static int selected_bitmap_player = -1;

static const u8 bitmap_colors[][3] = {
    {78, 205, 139},
    {100, 170, 245},
    {245, 180, 80},
    {235, 100, 150},
    {180, 140, 245},
    {80, 210, 220},
    {245, 110, 90},
    {210, 210, 110}
};

static void bitmap_draw_player(
    u8 *fb,
    int x,
    int y,
    int index
) {
    const int color_count =
        sizeof(bitmap_colors) / sizeof(bitmap_colors[0]);

    const u8 *color = bitmap_colors[index % color_count];

    bottom_rect(
        fb,
        x - 2,
        y - 2,
        5,
        5,
        color[0],
        color[1],
        color[2]
    );
}

static void bitmap_get_block_color(
    uint8_t block,
    u8 *r, u8 *g, u8 *b
) {
    switch (block) {
        case B_water:
        case B_water_1:
        case B_water_2:
        case B_water_3:
        case B_water_4:
        case B_water_5:
        case B_water_6:
        case B_water_7:
            *r = 35; *g = 105; *b = 190;
            break;

        case B_sand:
        case B_red_sand:
            *r = 218; *g = 195; *b = 125;
            break;

        case B_stone:
        case B_granite:
        case B_diorite:
        case B_andesite:
        case B_cobblestone:
            *r = 115; *g = 120; *b = 125;
            break;

        case B_dirt:
        case B_coarse_dirt:
        case B_podzol:
        case B_mud:
            *r = 120; *g = 80; *b = 48;
            break;

        case B_snow:
        case B_snow_block:
        case B_snowy_grass_block:
        case B_ice:
            *r = 215; *g = 230; *b = 240;
            break;

        case B_oak_leaves:
        case B_azalea_leaves:
            *r = 40; *g = 115; *b = 55;
            break;

        case B_oak_log:
            *r = 105; *g = 75; *b = 45;
            break;

        case B_lava:
        case B_lava_2:
        case B_lava_4:
        case B_lava_6:
            *r = 240; *g = 85; *b = 25;
            break;

        case B_grass_block:
            *r = 75; *g = 155; *b = 65;
            break;

        case B_bedrock:
            *r = 55; *g = 55; *b = 60;
            break;

        default:
            *r = 85; *g = 100; *b = 85;
            break;
    }
}

static uint8_t bitmap_get_surface_block(int x, int z) {
    int height = getHeightAt(x, z);

    if (height < 0) height = 0;
    if (height > 255) height = 255;

    // Vérifier l'eau au niveau de la mer.
    if (height < 63) {
        uint8_t water = getBlockAt(x, 63, z);

        if (water == B_water) {
            return water;
        }
    }

    return getBlockAt(x, height, z);
}

static void bitmap_update_cache(void) {
    for (int row = 0; row < MAP_ROWS; row++) {
        for (int col = 0; col < MAP_COLS; col++) {

            int dx = (col - MAP_COLS / 2) * MAP_RANGE / (MAP_COLS / 2);

            int dz = (MAP_ROWS / 2 - row) * MAP_RANGE / (MAP_ROWS / 2);

            int world_x = bitmap_center_x + dx;
            int world_z = bitmap_center_z + dz;

            bitmap_cache[row * MAP_COLS + col] = bitmap_get_surface_block(world_x, world_z);
        }
    }

    bitmap_cache_valid = true;
}

static void bitmap_update_center(void) {
    if (!bitmap_cache_valid) {
        int total_x = 0;
        int total_z = 0;
        int count = 0;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!dashboard_player_online(i)) continue;

            total_x += player_data[i].x;
            total_z += player_data[i].z;
            count++;
        }

        if (count > 0) {
            bitmap_center_x = total_x / count;
            bitmap_center_z = total_z / count;
        }

        bitmap_update_cache();
        return;
    }

    // Le joueur sélectionné sert de référence pour le déplacement.
    if (selected_bitmap_player < 0 ||
        !dashboard_player_online(selected_bitmap_player)) {
        return;
    }

    PlayerData *player = &player_data[selected_bitmap_player];

    int dx = (int)player->x - bitmap_center_x;
    int dz = (int)player->z - bitmap_center_z;

    if (dx >= -MAP_RECENTER_THRESHOLD && dx <= MAP_RECENTER_THRESHOLD && dz >= -MAP_RECENTER_THRESHOLD && dz <= MAP_RECENTER_THRESHOLD) {
        return;
    }

    // Le joueur approche du bord : on recentre la carte.
    bitmap_center_x = player->x;
    bitmap_center_z = player->z;

    bitmap_update_cache();
}


void dashboard_render_bitmap(void) {
    u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!fb) return;

    // Background
    bottom_rect(fb, 0, 0, 320, 240, 10, 13, 17);
    bottom_rect(fb, 6, 6, 308, 228, 24, 34, 46);

    // Header
    bottom_text(fb, 14, 12, "PLAYER BITMAP", 235, 240, 245);
    bottom_rect(fb, 12, 21, 296, 1, 105, 126, 145);

    draw_bottom_tabs(fb);

    // Count online players.
    int count = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (dashboard_player_online(i)) {
            count++;
        }
    }

    // Clear selection if the player disconnected.
    if (selected_bitmap_player >= 0 &&
        !dashboard_player_online(selected_bitmap_player)) {
        selected_bitmap_player = -1;
    }

    if (count == 0) {
        bitmap_cache_valid = false;
        bottom_text(fb, 105, 105, "NO PLAYERS ONLINE", 155, 174, 190);
        return;
    }

    // Update the map only when necessary.
    bitmap_update_center();

    // Draw terrain background.
    bottom_rect(fb, MAP_X, MAP_Y, MAP_W, MAP_H, 15, 24, 32);

    // Each cached cell fills exactly 2 x 2 pixels.
    for (int row = 0; row < MAP_ROWS; row++) {
        for (int col = 0; col < MAP_COLS; col++) {
            uint8_t block = bitmap_cache[row * MAP_COLS + col];

            u8 r, g, b;
            bitmap_get_block_color(block, &r, &g, &b);

            bottom_rect(fb, MAP_X + col * 2, MAP_Y + row * 2, 2, 2, r, g, b);
        }
    }

    // Center marker.
    bottom_rect(fb, MAP_X + MAP_W / 2 - 2, MAP_Y + MAP_H / 2 - 2, 5, 5, 235, 240, 245);

    // Draw online players.
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!dashboard_player_online(i)) continue;

        int dx = (int)player_data[i].x - bitmap_center_x;
        int dz = (int)player_data[i].z - bitmap_center_z;

        int px = MAP_X + MAP_W / 2 + dx * (MAP_W / 2 - 5) / MAP_RANGE;

        int py = MAP_Y + MAP_H / 2 - dz * (MAP_H / 2 - 5) / MAP_RANGE;

        // Keep markers inside the map.
        if (px < MAP_X + 3) px = MAP_X + 3;
        if (px > MAP_X + MAP_W - 3) px = MAP_X + MAP_W - 3;
        if (py < MAP_Y + 3) py = MAP_Y + 3;
        if (py > MAP_Y + MAP_H - 3) py = MAP_Y + MAP_H - 3;

        bitmap_draw_player(fb, px, py, i);

        // White outline for selected player.
        if (i == selected_bitmap_player) {
            bottom_rect(fb, px - 4, py - 4, 9, 1, 235, 240, 245);
            bottom_rect(fb, px - 4, py + 4, 9, 1, 235, 240, 245);
            bottom_rect(fb, px - 4, py - 3, 1, 7, 235, 240, 245);
            bottom_rect(fb, px + 4, py - 3, 1, 7, 235, 240, 245);
        }
    }

    // Selected player information.
    if (selected_bitmap_player >= 0) {
        PlayerData *player = &player_data[selected_bitmap_player];

        char info[64];
        snprintf(info, sizeof(info), "%.*s X:%d Y:%d Z:%d", 16, player->name, (int)player->x, (int)player->y, (int)player->z);

        bottom_text(fb, 16, 190, info, 235, 240, 245);
    } else {
        bottom_text(fb, 16, 190, "TOUCH A PLAYER", 155, 174, 190);
    }

    // Current map center.
    char center_info[32];
    snprintf(center_info, sizeof(center_info), "CENTER X:%d Z:%d", bitmap_center_x, bitmap_center_z);

    bottom_text(fb, 16, 205, center_info, 155, 174, 190);
}


void dashboard_render_settings(void) {
    u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!fb) return;

    bottom_rect(fb, 0, 0, 320, 240, 10, 13, 17);
    bottom_rect(fb, 6, 6, 308, 228, 24, 34, 46);

    bottom_text(fb, 14, 12, "SERVER SETTINGS", 235, 240, 245);
    bottom_rect(fb, 12, 21, 296, 1, 105, 126, 145);

    draw_bottom_tabs(fb);

    // VIEW DISTANCE
    bottom_rect(fb, 12, 55, 296, 52, 30, 42, 55);
    bottom_text(fb, 20, 65, "VIEW DISTANCE", 235, 240, 245);

    char view_text[12];
    snprintf(view_text, sizeof(view_text), "%d", server_view_distance);
    bottom_text(fb, 155, 80, view_text, 130, 205, 150);

    bottom_rect(fb, 205, 65, 40, 30, 65, 75, 90);
    bottom_text(fb, 220, 77, "-", 235, 240, 245);

    bottom_rect(fb, 260, 65, 40, 30, 65, 75, 90);
    bottom_text(fb, 275, 77, "+", 235, 240, 245);

    // MAX PLAYERS
    bottom_rect(fb, 12, 120, 296, 52, 30, 42, 55);
    bottom_text(fb, 20, 130, "MAX PLAYERS", 235, 240, 245);

    char players_text[12];
    snprintf(players_text, sizeof(players_text), "%d", server_max_clients);
    bottom_text(fb, 155, 145, players_text, 130, 205, 150);

    bottom_rect(fb, 205, 130, 40, 30, 65, 75, 90);
    bottom_text(fb, 220, 142, "-", 235, 240, 245);

    bottom_rect(fb, 260, 130, 40, 30, 65, 75, 90);
    bottom_text(fb, 275, 142, "+", 235, 240, 245);

    bottom_text(fb, 20, 195, "TOUCH +/- TO CHANGE", 155, 174, 190);
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

    if (current_tab == TAB_SETTINGS) {
        dashboard_render_settings();
        return;
    }

    if (current_tab == TAB_BITMAP) {
        dashboard_render_bitmap();
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
        "PLAYER BITMAP",
        "SERVER LOGS",
        "SERVER SETTINGS"
    };


    bottom_text(fb, 14, 12, titles[current_tab], 235, 240, 245);
    bottom_rect(fb, 12, 21, 296, 1, 105, 126, 145);

    draw_bottom_tabs(fb);

    bottom_text(fb, 116, 115, "COMING SOON", 155, 174, 190);
}

void dashboard_handle_touch(int x, int y) {
    if (current_tab == TAB_SETTINGS) {
        // View Distance: minus
        if (x >= 205 && x <= 245 && y >= 65 && y <= 95) {
            if (server_view_distance > 1) {
                server_view_distance--;
            }
            return;
        }

        // View Distance: plus
        if (x >= 260 && x <= 300 && y >= 65 && y <= 95) {
            if (server_view_distance < 4) {
                server_view_distance++;
            }
            return;
        }

        // Max Players: minus
        if (x >= 205 && x <= 245 && y >= 130 && y <= 160) {
            if (server_max_clients > 1) {
                server_max_clients--;
            }
            return;
        }

        // Max Players: plus
        if (x >= 260 && x <= 300 && y >= 130 && y <= 160) {
            if (server_max_clients < 10) {
                server_max_clients++;
            }
            return;
        }

        return;
    }

    if (current_tab == TAB_BITMAP) {
        if (!bitmap_cache_valid) return;

        selected_bitmap_player = -1;
        int best_distance = 100;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!dashboard_player_online(i)) continue;

            int dx = (int)player_data[i].x - bitmap_center_x;
            int dz = (int)player_data[i].z - bitmap_center_z;

            int px = MAP_X + MAP_W / 2 + dx * (MAP_W / 2 - 5) / MAP_RANGE;

            int py = MAP_Y + MAP_H / 2 - dz * (MAP_H / 2 - 5) / MAP_RANGE;

            if (px < MAP_X + 3) px = MAP_X + 3;
            if (px > MAP_X + MAP_W - 3) px = MAP_X + MAP_W - 3;
            if (py < MAP_Y + 3) py = MAP_Y + 3;
            if (py > MAP_Y + MAP_H - 3) py = MAP_Y + MAP_H - 3;

            int tx = x - px;
            int ty = y - py;
            int distance = tx * tx + ty * ty;

            if (distance <= 100 && distance < best_distance) {
                best_distance = distance;
                selected_bitmap_player = i;
            }
        }

        return;
    }

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
