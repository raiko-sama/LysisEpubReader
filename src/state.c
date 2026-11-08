#include "state.h"
#include "util.h"
#include <string.h>
#include <stdio.h>

Config g_cfg;

/* shadow of the last configuration written to SD: st_save_cfg only writes
   the files whose value actually changed (first save writes everything). */
static Config g_saved;
static bool   g_saved_init = false;

static void cfg_defaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.dark_mode = true;
    g_cfg.images = true;
    g_cfg.flip = false;
    g_cfg.autorot = false;
    g_cfg.hourly_refresh = false;
    g_cfg.cover_poweroff = true;
    g_cfg.auto_off_min = UI_AUTOOFF_DEFAULT;
    g_cfg.utc_offset = 0;
    g_cfg.alarm_on = false;
    g_cfg.alarm_h = 7;
    g_cfg.alarm_m = 0;
    g_cfg.wake_type = WAKE_NONE;
    g_cfg.last_chapter = 0;
    g_cfg.last_page = 0;
    g_cfg.rd_font = 1;      /* medium */
    g_cfg.rd_spacing = 0;   /* compact, pitch 27 px @100% */
    g_cfg.rd_margins = 1;   /* normal */
}

void st_load_cfg(void)
{
    cfg_defaults();
    g_cfg.dark_mode        = u_file_read_int(ST_DARK_MODE, 1) != 0;
    g_cfg.images           = u_file_read_int(ST_IMAGES, 1) != 0;
    g_cfg.flip             = u_file_read_int(ST_DISPLAY_FLIP, 0) != 0;
    g_cfg.autorot          = u_file_read_int(ST_AUTO_ROTATION, 1) != 0;
    g_cfg.rot_lock         = u_file_read_int(ST_ROTATION_LOCK, 0) != 0;
    g_cfg.orientation      = u_file_read_int(ST_ORIENTATION, 0);
    if (g_cfg.orientation < 0 || g_cfg.orientation > 3) g_cfg.orientation = 0;
    g_cfg.hourly_refresh   = u_file_read_int(ST_HOURLY_REFRESH, 0) != 0;
    g_cfg.cover_poweroff   = u_file_read_int(ST_COVER_POWEROFF, 1) != 0;
    g_cfg.auto_off_min     = u_file_read_int(ST_AUTO_OFF, UI_AUTOOFF_DEFAULT);
    g_cfg.utc_offset       = u_file_read_int(ST_UTC_OFFSET, 0);
    g_cfg.wake_type        = u_file_read_int(ST_WAKE_TYPE, WAKE_NONE);
    g_cfg.last_rotation    = u_file_read_int(ST_LAST_ROTATION, 0);

    int alarm[2] = {0, 0};
    FILE* f = fopen(ST_ALARM, "rb");
    if (f) {
        fread(alarm, sizeof(alarm), 1, f);
        fclose(f);
        g_cfg.alarm_on = alarm[0] != 0;
        g_cfg.alarm_h = alarm[1] / 60;
        g_cfg.alarm_m = alarm[1] % 60;
    }

    /* unified format: 8-byte epoch, written only when the clock was synced */
    g_cfg.ntp_epoch = u_file_read_i64(ST_NTP_SYNC, 0);
    g_cfg.ntp_synced = g_cfg.ntp_epoch > 1700000000;

    u_file_read_str(ST_LAST, g_cfg.last_book, sizeof(g_cfg.last_book));
    char* sep = strchr(g_cfg.last_book, '\n');
    if (sep) {
        *sep = 0;
        sscanf(sep + 1, "%d %d", &g_cfg.last_chapter, &g_cfg.last_page);
    }

    {
        int rd[3] = { g_cfg.rd_font, g_cfg.rd_spacing, g_cfg.rd_margins };
        FILE* f = fopen(ST_READING, "rb");
        if (f) {
            if (fread(rd, sizeof(rd), 1, f) == 1) {
                g_cfg.rd_font = rd[0] < 0 || rd[0] > 5 ? 1 : rd[0];
                g_cfg.rd_spacing = rd[1] < 0 || rd[1] > 2 ? 0 : rd[1];
                g_cfg.rd_margins = rd[2] < 0 || rd[2] > 2 ? 1 : rd[2];
            }
            fclose(f);
        }
    }

    g_saved = g_cfg;
    g_saved_init = true;
}

void st_save_cfg(void)
{
    if (g_saved_init && memcmp(&g_cfg, &g_saved, sizeof(g_cfg)) == 0) return;
    bool full = !g_saved_init;

    if (full || g_cfg.dark_mode      != g_saved.dark_mode)      u_file_write_int(ST_DARK_MODE, g_cfg.dark_mode);
    if (full || g_cfg.images         != g_saved.images)         u_file_write_int(ST_IMAGES, g_cfg.images);
    if (full || g_cfg.flip           != g_saved.flip)           u_file_write_int(ST_DISPLAY_FLIP, g_cfg.flip);
    if (full || g_cfg.autorot        != g_saved.autorot)        u_file_write_int(ST_AUTO_ROTATION, g_cfg.autorot);
    if (full || g_cfg.rot_lock       != g_saved.rot_lock)       u_file_write_int(ST_ROTATION_LOCK, g_cfg.rot_lock);
    if (full || g_cfg.orientation    != g_saved.orientation)    u_file_write_int(ST_ORIENTATION, g_cfg.orientation);
    if (full || g_cfg.hourly_refresh != g_saved.hourly_refresh) u_file_write_int(ST_HOURLY_REFRESH, g_cfg.hourly_refresh);
    if (full || g_cfg.cover_poweroff != g_saved.cover_poweroff) u_file_write_int(ST_COVER_POWEROFF, g_cfg.cover_poweroff);
    if (full || g_cfg.auto_off_min   != g_saved.auto_off_min)   u_file_write_int(ST_AUTO_OFF, g_cfg.auto_off_min);
    if (full || g_cfg.utc_offset     != g_saved.utc_offset)     u_file_write_int(ST_UTC_OFFSET, g_cfg.utc_offset);
    if (full || g_cfg.last_rotation  != g_saved.last_rotation)  u_file_write_int(ST_LAST_ROTATION, g_cfg.last_rotation);

    if (full || g_cfg.alarm_on != g_saved.alarm_on ||
        g_cfg.alarm_h != g_saved.alarm_h || g_cfg.alarm_m != g_saved.alarm_m) {
        int alarm[2] = { g_cfg.alarm_on ? 1 : 0, g_cfg.alarm_h * 60 + g_cfg.alarm_m };
        u_file_write_atomic(ST_ALARM, alarm, sizeof(alarm));
    }

    if (full || g_cfg.ntp_synced != g_saved.ntp_synced || g_cfg.ntp_epoch != g_saved.ntp_epoch) {
        int64_t ep = g_cfg.ntp_synced ? g_cfg.ntp_epoch : 0;
        u_file_write_atomic(ST_NTP_SYNC, &ep, sizeof(ep));
    }

    if (full || strcmp(g_cfg.last_book, g_saved.last_book) != 0 ||
        g_cfg.last_chapter != g_saved.last_chapter || g_cfg.last_page != g_saved.last_page) {
        char buf[300];
        snprintf(buf, sizeof(buf), "%s\n%d %d\n", g_cfg.last_book, g_cfg.last_chapter, g_cfg.last_page);
        u_file_write_atomic(ST_LAST, buf, strlen(buf));
    }

    if (full || g_cfg.rd_font != g_saved.rd_font ||
        g_cfg.rd_spacing != g_saved.rd_spacing || g_cfg.rd_margins != g_saved.rd_margins) {
        int rd[3] = { g_cfg.rd_font, g_cfg.rd_spacing, g_cfg.rd_margins };
        u_file_write_atomic(ST_READING, rd, sizeof(rd));
    }

    g_saved = g_cfg;
    g_saved_init = true;
}

int st_init(void)
{
    u_mkdirs(BOOKS_DIR);
    u_mkdirs(ST_DIR);
    st_load_cfg();
    return 0;
}

/* State files are keyed by the FULL book path (CRC32) so two books with the
   same filename in different folders keep separate progress/bookmarks. */
void st_book_path(const char* bookpath, char* out, int outlen)
{
    uint32_t id = u_crc32(bookpath, strlen(bookpath));
    snprintf(out, outlen, ST_DIR "/%08x.dat", id);
}

/* legacy naming (basename only) used by the original firmware layout */
void st_book_path_legacy(const char* bookpath, char* out, int outlen)
{
    char name[160];
    snprintf(name, sizeof(name), "%s", u_basename(bookpath));
    u_strip_ext(name);
    /* sanitize */
    for (char* p = name; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"') *p = '_';
    }
    snprintf(out, outlen, ST_DIR "/%s.dat", name);
}

typedef struct {
    BookState bs;
    uint32_t  crc;   /* CRC32 of the BookState payload */
} BookStateFile;

static void bs_sanitize(BookState* st)
{
    if (st->bm_count < 0) st->bm_count = 0;
    if (st->bm_count > BM_MAX) st->bm_count = BM_MAX;
    if (st->furthest_ch < 0) st->furthest_ch = 0;
    if (st->furthest_pg < 0) st->furthest_pg = 0;
    if (st->last_ch < 0) st->last_ch = 0;
    if (st->last_pg < 0) st->last_pg = 0;
    if (st->last_off < -1) st->last_off = -1;
    for (int i = 0; i < st->bm_count; i++) {
        if (st->bm[i].ch < 0) st->bm[i].ch = 0;
        if (st->bm[i].pg < 0) st->bm[i].pg = 0;
    }
}

static int g_st_gets = 0;
int st_book_get_count(void) { return g_st_gets; }

int st_book_get(const char* bookpath, BookState* st)
{
    g_st_gets++;
    memset(st, 0, sizeof(*st));
    st->magic = BOOKSTATE_MAGIC;
    st->last_off = -1;
    for (int i = 0; i < 256; i++) st->page_counts[i] = -1;
    char path[256];
    st_book_path(bookpath, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) {
        /* migration: try the legacy basename-only file */
        st_book_path_legacy(bookpath, path, sizeof(path));
        f = fopen(path, "rb");
    }
    if (!f) return -1;
    BookStateFile tmp;
    size_t rd = fread(&tmp, 1, sizeof(tmp), f);
    fclose(f);
    if (rd == sizeof(tmp) && tmp.bs.magic == BOOKSTATE_MAGIC &&
        tmp.crc == u_crc32(&tmp.bs, sizeof(tmp.bs))) {
        *st = tmp.bs;                      /* current format with crc */
        bs_sanitize(st);
        return 0;
    }
    if (rd == (size_t)BS_LEGACY_V1 + 4 && tmp.bs.magic == BOOKSTATE_MAGIC) {
        /* v1 (pre-offset) payload + crc: the stored crc lands on last_off */
        uint32_t crc_v1 = (uint32_t)tmp.bs.last_off;
        if (crc_v1 == u_crc32(&tmp.bs, BS_LEGACY_V1)) {
            *st = tmp.bs;
            st->last_off = -1;
            st->lkey = 0;
            bs_sanitize(st);
            return 0;
        }
    }
    if (rd == (size_t)BS_LEGACY_V1 && tmp.bs.magic == BOOKSTATE_MAGIC) {
        *st = tmp.bs;                      /* original format without crc */
        st->last_off = -1;
        st->lkey = 0;
        bs_sanitize(st);
        return 0;
    }
    return -1;
}

void st_book_put(const char* bookpath, const BookState* st)
{
    char path[256];
    st_book_path(bookpath, path, sizeof(path));
    BookStateFile out;
    memcpy(&out.bs, st, sizeof(*st));
    out.crc = u_crc32(st, sizeof(*st));
    u_file_write_atomic(path, &out, sizeof(out));
}

int st_total_books_progress(void)
{
    return -1; /* reserved: aggregate progress across library */
}

// ------------------------------------------------------------------ wifi nets
#define WIFI_MAGIC 0x4C595357   /* "LYSW" */
typedef struct { char ssid[33]; char pass[65]; } NetCred;
typedef struct {
    int32_t magic;
    int32_t count;
    NetCred nets[WNET_MAX];
} NetTable;

static NetTable g_nets;
static bool g_nets_loaded = false;

static void nets_load(void)
{
    if (g_nets_loaded) return;
    g_nets_loaded = true;
    memset(&g_nets, 0, sizeof(g_nets));
    FILE* f = fopen(ST_WIFI, "rb");
    if (!f) return;
    NetTable tmp;
    size_t rd = fread(&tmp, 1, sizeof(tmp), f);
    fclose(f);
    if (rd == sizeof(tmp) && tmp.magic == WIFI_MAGIC) {
        if (tmp.count < 0) tmp.count = 0;
        if (tmp.count > WNET_MAX) tmp.count = WNET_MAX;
        for (int i = 0; i < WNET_MAX; i++) { tmp.nets[i].ssid[32] = 0; tmp.nets[i].pass[64] = 0; }
        g_nets = tmp;
    }
}

static void nets_save(void)
{
    g_nets.magic = WIFI_MAGIC;
    u_file_write_atomic(ST_WIFI, &g_nets, sizeof(g_nets));
}

void st_wifi_save(const char* ssid, const char* pass)
{
    if (!ssid || !ssid[0]) return;
    nets_load();
    int idx = -1;
    for (int i = 0; i < g_nets.count; i++) {
        if (strcmp(g_nets.nets[i].ssid, ssid) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        int keep = g_nets.count >= WNET_MAX ? WNET_MAX - 1 : g_nets.count;
        memmove(&g_nets.nets[1], &g_nets.nets[0], sizeof(NetCred) * keep);
        if (g_nets.count < WNET_MAX) g_nets.count++;
        idx = 0;
    } else if (idx > 0) {
        NetCred tmp = g_nets.nets[idx];
        memmove(&g_nets.nets[1], &g_nets.nets[0], sizeof(NetCred) * idx);
        g_nets.nets[0] = tmp;
        idx = 0;
    }
    snprintf(g_nets.nets[idx].ssid, sizeof(g_nets.nets[idx].ssid), "%s", ssid);
    snprintf(g_nets.nets[idx].pass, sizeof(g_nets.nets[idx].pass), "%s", pass ? pass : "");
    nets_save();
}

const char* st_wifi_find(const char* ssid)
{
    if (!ssid || !ssid[0]) return NULL;
    nets_load();
    for (int i = 0; i < g_nets.count; i++) {
        if (strcmp(g_nets.nets[i].ssid, ssid) == 0) return g_nets.nets[i].pass;
    }
    return NULL;
}

int st_wifi_count(void)
{
    nets_load();
    return g_nets.count;
}

int st_wifi_get(int i, char* ssid, int sslen, char* pass, int palen)
{
    nets_load();
    if (i < 0 || i >= g_nets.count) return -1;
    if (ssid && sslen > 0) snprintf(ssid, sslen, "%s", g_nets.nets[i].ssid);
    if (pass && palen > 0) snprintf(pass, palen, "%s", g_nets.nets[i].pass);
    return 0;
}

/* W1: fast-connect cache (magic LYSF + crc32(ssid) + bssid + channel + crc) */
#define WIFI_FAST_MAGIC 0x4C595346   /* "LYSF" */
typedef struct {
    int32_t  magic;
    uint32_t ssid_crc;
    uint8_t  bssid[6];
    int16_t  channel;
    uint32_t crc;
} WifiFastFile;

bool st_wifi_fast_get(const char* ssid, uint8_t bssid_out[6], int* channel_out)
{
    if (!ssid || !ssid[0]) return false;
    WifiFastFile f;
    FILE* fp = fopen(ST_WIFI_FAST, "rb");
    if (!fp) return false;
    size_t rd = fread(&f, 1, sizeof(f), fp);
    fclose(fp);
    if (rd != sizeof(f) || f.magic != WIFI_FAST_MAGIC) return false;
    if (f.crc != u_crc32(&f, sizeof(f) - sizeof(f.crc))) return false;
    if (f.ssid_crc != u_crc32(ssid, strlen(ssid))) return false;
    if (f.channel < 1 || f.channel > 14) return false;
    if (bssid_out) memcpy(bssid_out, f.bssid, 6);
    if (channel_out) *channel_out = (int)f.channel;
    return true;
}

void st_wifi_fast_save(const char* ssid, const uint8_t bssid[6], int channel)
{
    if (!ssid || !ssid[0]) return;
    WifiFastFile f;
    memset(&f, 0, sizeof(f));
    f.magic = WIFI_FAST_MAGIC;
    f.ssid_crc = u_crc32(ssid, strlen(ssid));
    if (bssid) memcpy(f.bssid, bssid, 6);
    f.channel = (int16_t)(channel >= 1 && channel <= 14 ? channel : 0);
    f.crc = u_crc32(&f, sizeof(f) - sizeof(f.crc));
    u_file_write_atomic(ST_WIFI_FAST, &f, sizeof(f));
}
