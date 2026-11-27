// host/host_main.c - headless scripted UI run of the real new app.
// Commands: tap X Y | swipe X0 Y0 X1 Y1 | pwr | wait MS | shot NAME
//           netadd SSID PASS | quit
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "bridge.h"

extern void app_setup(void);
extern int  app_loop(void);
extern void host_schedule_touch(int x0, int y0, int x1, int y1, uint32_t at_ms);
extern void host_schedule_touch_ms(int x0, int y0, int x1, int y1, uint32_t at_ms, uint32_t hold_ms);
extern void host_schedule_pwr(uint32_t at_ms);
extern void host_schedule_shot(const char* name, uint32_t at_ms);
extern void host_shots_pending(void);
extern void host_shot(const char* name);
extern void host_net_add(const char* ssid, const char* pass);

typedef enum { C_TAP, C_SWIPE, C_PWR, C_WAIT, C_SHOT, C_NETADD, C_QUIT } CmdType;
typedef struct {
    CmdType type; int a, b, c, d; uint32_t at; char name[32], pass[64];
} Cmd;

static Cmd cmds[512];
static int ncmds;

static int parse(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); return -1; }
    char line[256];
    uint32_t at = 0;
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == 0) continue;
        char cmd[32] = {0};
        if (sscanf(p, "%31s", cmd) != 1) continue;
        Cmd c; memset(&c, 0, sizeof(c)); c.at = at;
        if (!strcmp(cmd, "tap")) {
            if (sscanf(p + 3, "%d %d", &c.a, &c.b) != 2) continue;
            c.type = C_TAP;
        } else if (!strcmp(cmd, "swipe")) {
            if (sscanf(p + 5, "%d %d %d %d", &c.a, &c.b, &c.c, &c.d) != 4) continue;
            c.type = C_SWIPE;
        } else if (!strcmp(cmd, "pwr")) {
            c.type = C_PWR;
        } else if (!strcmp(cmd, "wait")) {
            int ms = 0;
            if (sscanf(p + 4, "%d", &ms) != 1) continue;
            at += (uint32_t)ms;
            continue;
        } else if (!strcmp(cmd, "shot")) {
            if (sscanf(p + 4, "%31s", c.name) != 1) continue;
            c.type = C_SHOT;
        } else if (!strcmp(cmd, "netadd")) {
            if (sscanf(p + 6, "%31s %63s", c.name, c.pass) != 2) continue;
            c.type = C_NETADD;
        } else if (!strcmp(cmd, "quit")) {
            c.type = C_QUIT;
        } else {
            fprintf(stderr, "unknown command: %s\n", cmd);
            continue;
        }
        if (ncmds < (int)(sizeof(cmds)/sizeof(cmds[0]))) cmds[ncmds++] = c;
    }
    fclose(f);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: host_gui script.txt\n");
        return 2;
    }
    if (parse(argv[1]) != 0) return 2;
    app_setup();
    uint32_t base = brd_millis();
    int next = 0;
    uint32_t last_at = 0;
    for (;;) {
        uint32_t now = brd_millis();
        while (next < ncmds && now >= base + cmds[next].at) {
            Cmd* c = &cmds[next++];
            switch (c->type) {
            case C_TAP: host_schedule_touch(c->a, c->b, c->a, c->b, now + 10); break;
            case C_SWIPE: host_schedule_touch(c->a, c->b, c->c, c->d, now + 10); break;
            case C_PWR: host_schedule_pwr(now + 10); break;
            case C_SHOT: host_schedule_shot(c->name, now + 10); break;
            case C_NETADD: host_net_add(c->name, c->pass); break;
            case C_QUIT: break;
            default: break;
            }
            if (c->at > last_at) last_at = c->at;
        }
        app_loop();
        host_shots_pending();
        if (next >= ncmds && now > base + last_at + 800) break;
        Sleep(1);
    }
    host_shots_pending();
    host_shot("final");
    printf("DONE frames=%u\n", (unsigned)brd_refresh_count());
    return 0;
}
