#include "bmflat.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct bm_log *bm_logs = NULL;
static int log_cap, log_ptr;

static void reset_logs()
{
    if (bm_logs) free(bm_logs);
    bm_logs = NULL;
    log_cap = log_ptr = 0;
}

static void ensure_log_cap()
{
    if (log_cap <= log_ptr) {
        log_cap = (log_cap == 0 ? 8 : (log_cap << 1));
        bm_logs = (struct bm_log *)
            realloc(bm_logs, log_cap * sizeof(struct bm_log));
    }
}

#define emit_log(_line, ...) do { \
    ensure_log_cap(); \
    bm_logs[log_ptr].line = _line; \
    snprintf(bm_logs[log_ptr].message, BM_MSG_LEN, __VA_ARGS__); \
    log_ptr++; \
} while (0)

static inline int is_space_or_linebreak(char ch)
{
    return ch == '\r' || ch == '\n' || ch == '\0';
}

static inline int isbase36(char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z');
}

static inline int base36(char c1, char c2)
{
    // Assumes isbase36(c1) and isbase36(c2) are true
    return
        (c1 <= '9' ? c1 - '0' : c1 - 'A' + 10) * 36 +
        (c2 <= '9' ? c2 - '0' : c2 - 'A' + 10);
}

static inline void add_note(struct bm_track *track, short bar, float beat, short value)
{
    if (track->note_cap <= track->note_count) {
        track->note_cap = (track->note_cap == 0 ? 8 : (track->note_cap << 1));
        track->notes = (struct bm_note *)
            realloc(track->notes, track->note_cap * sizeof(struct bm_note));
    }
    track->notes[track->note_count].bar = bar;
    track->notes[track->note_count].beat = beat;
    track->notes[track->note_count++].value = value;
}

static inline void parse_track(int line, char *s, struct bm_track *track, short bar)
{
    int count = 0;
    for (char *p = s; *p != '\0'; p++) count += (!isspace(*p));
    count /= 2;

    for (int p = 0, q, i = 0; s[p] != '\0'; p = q + 1) {
        while (isspace(s[p]) && s[p] != '\0') p++;
        if (s[p] == '\0') break;
        q = p + 1;
        while (isspace(s[q]) && s[q] != '\0') q++;
        if (s[q] == '\0') {
            emit_log(line, "Extraneous trailing character %c, ignoring", s[p]);
            break;
        }
        if (!isbase36(s[p]) || !isbase36(s[q])) {
            emit_log(line, "Invalid base-36 index %c%c at column %d, ignoring",
                s[p], s[q], p + 8);
            continue;
        }
        int value = base36(s[p], s[q]);
        if (value != 0) add_note(track, bar, (float)i / count, value);
        i++;
    }
}

static int note_time_compare(const void *_lhs, const void *_rhs)
{
    const struct bm_note *lhs = (const struct bm_note *)_lhs;
    const struct bm_note *rhs = (const struct bm_note *)_rhs;
    float diff = (lhs->bar - rhs->bar) + (lhs->beat - rhs->beat);
    return (diff < -1e-6 ? -1 : (diff > +1e-6 ? +1 : 0));
}

static inline void sort_track(struct bm_track *track)
{
    // A stable sorting algorithm
    mergesort(track->notes, track->note_count,
        sizeof(struct bm_note), note_time_compare);
    // Remove duplicates
    int p, q;
    float last_time = -1;
    for (p = 0, q = -1; p < track->note_count; p++) {
        float cur_time = track->notes[p].bar + track->notes[p].beat;
        if (cur_time - last_time > 1e-6) q++;
        if (p != q) track->notes[q] = track->notes[p];
        last_time = cur_time;
    }
    track->note_count = q + 1;
}

int bm_load(struct bm_chart *chart, const char *_source)
{
    char *source = strdup(_source);

    chart->meta.player_num = -1;
    chart->meta.genre = NULL;
    chart->meta.title = NULL;
    chart->meta.artist = NULL;
    chart->meta.subartist = NULL;
    chart->meta.init_tempo = -1;
    chart->meta.play_level = -1;
    chart->meta.judge_rank = -1;
    chart->meta.gauge_total = -1;
    chart->meta.difficulty = -1;    // Omissible
    chart->meta.stage_file = NULL;
    chart->meta.banner = NULL;
    chart->meta.back_bmp = NULL;
    memset(&chart->tables.wav, 0, sizeof chart->tables.wav);
    memset(&chart->tables.bmp, 0, sizeof chart->tables.bmp);
    for (int i = 0; i < BM_INDEX_MAX; i++) chart->tables.tempo[i] = -1;
    memset(&chart->tables.stop, -1, sizeof chart->tables.stop);
    memset(&chart->tracks, 0, sizeof chart->tracks);

    reset_logs();
    int len = strlen(source);
    int ptr = 0, next = 0, line = 1;

    // Temporary storage
    int bg_index[BM_BARS_COUNT] = { 0 };
    bool track_appeared[BM_BARS_COUNT][60] = { false };
    int lnobj = -1;

    for (; ptr != len; ptr = ++next, line++) {
        // Advance to the next line break
        while (!is_space_or_linebreak(source[next])) next++;
        if (source[next] == '\r' && next + 1 < len && source[next + 1] == '\n') next++;

        // Trim at both ends
        while (ptr < next && isspace(source[ptr])) ptr++;
        int end = next;
        while (end >= ptr && isspace(source[end])) end--;
        source[++end] = '\0';

        // Comment
        if (source[ptr] != '#') continue;

        // Skip the # character
        char *s = source + ptr + 1;
        int line_len = end - ptr - 1;

        if (line_len >= 6 && isdigit(s[0]) && isdigit(s[1]) && isdigit(s[2]) &&
            isdigit(s[3]) && isdigit(s[4]) && s[5] == ':')
        {
            // Track data
            int bar = s[0] * 100 + s[1] * 10 + s[2] - '0' * 111;
            int track = s[3] * 10 + s[4] - '0' * 11;

            if (track >= 3 && track <= 69 && track != 5 && track % 10 != 0 &&
                track_appeared[bar][track])
            {
                emit_log(line, "Track %02d already defined previously, "
                    "merging all notes", track);
            }
            track_appeared[bar][track] = true;

            if (track == 2) {
                // Time signature
                errno = 0;
                float x = strtof(s + 6, NULL);
                if (errno != EINVAL && x >= 0.25 && x <= 63.75) {
                    int y = (int)(x * 4 + 0.5);
                    if (fabs(y - x * 4) >= 1e-3)
                        emit_log(line, "Inaccurate time signature, treating as %d/4", y);
                    if (chart->tracks.time_sig[bar] != 0)
                        emit_log(line, "Time signature for bar %03d "
                            "defined multiple times, overwriting", bar);
                    chart->tracks.time_sig[bar] = y;
                } else {
                    emit_log(line, "Invalid time signature, should be a "
                        "multiple of 0.25 between 0.25 and 63.75 (inclusive)");
                }
            } else if (track == 3) {
                // Tempo change
                parse_track(line, s + 6, &chart->tracks.tempo, bar);
            } else if (track == 4) {
                // BGA
                parse_track(line, s + 6, &chart->tracks.bga_base, bar);
            } else if (track == 6) {
                // BGA poor
                parse_track(line, s + 6, &chart->tracks.bga_poor, bar);
            } else if (track == 7) {
                // BGA layer
                parse_track(line, s + 6, &chart->tracks.bga_layer, bar);
            } else if (track == 8) {
                // Extended tempo change
                parse_track(line, s + 6, &chart->tracks.ex_tempo, bar);
            } else if (track == 9) {
                // Stop
                parse_track(line, s + 6, &chart->tracks.stop, bar);
            } else if (track >= 10 && track <= 69 && track % 10 != 0) {
                // Fixed
                parse_track(line, s + 6, &chart->tracks.fixed[track - 10], bar);
            } else if (track == 1) {
                if (bg_index[bar] == BM_BGM_TRACKS) {
                    emit_log(line, "Too many background tracks (more than %d) "
                        "for bar %03d, ignoring", BM_BGM_TRACKS, bar);
                } else {
                    parse_track(line, s + 6, &chart->tracks.background[bg_index[bar]], bar);
                    bg_index[bar]++;
                }
            } else {
                emit_log(line, "Unknown track %c%c, ignoring", s[3], s[4]);
            }
        } else {
            // Command
            int arg = 0;
            while (arg < line_len && !isspace(s[arg])) arg++;
            s[arg++] = '\0';
            while (arg < line_len && isspace(s[arg])) arg++;

            if (arg >= line_len) {
                emit_log(line, "Command requires non-empty arguments, ignoring");
                continue;
            }

            #define checked_parse_int(_var, _min, _max, ...) do { \
                errno = 0; \
                long x = strtol(s + arg, NULL, 10); \
                if (errno != EINVAL && x >= (_min) && x <= (_max)) { \
                    if ((_var) != -1) emit_log(line, __VA_ARGS__); \
                    (_var) = x; \
                } else { \
                    emit_log(line, "Invalid integral value, should be " \
                        "between %d and %d (inclusive)", (_min) ,(_max)); \
                } \
            } while (0)

            #define checked_parse_float(_var, _min, _max, ...) do { \
                errno = 0; \
                float x = strtof(s + arg, NULL); \
                if (errno != EINVAL && x >= (_min) && x <= (_max)) { \
                    if ((_var) != -1) emit_log(line, __VA_ARGS__); \
                    (_var) = x; \
                } else { \
                    emit_log(line, "Invalid integral value, should be " \
                        "between %g and %g (inclusive)", (_min) ,(_max)); \
                } \
            } while (0)

            #define checked_strdup(_var, ...) do { \
                char *x = strdup(s + arg); \
                /* TODO: Handle cases of memory exhaustion? */ \
                if (x != NULL) { \
                    if ((_var) != NULL) { free(_var); emit_log(line, __VA_ARGS__); } \
                    (_var) = x; \
                } \
            } while (0)

            if (strcmp(s, "PLAYER") == 0) {
                checked_parse_int(chart->meta.player_num,
                    1, 3,
                    "Multiple PLAYER commands, overwritten");
            } else if (strcmp(s, "GENRE") == 0) {
                checked_strdup(chart->meta.genre,
                    "Multiple GENRE commands, overwritten");
            } else if (strcmp(s, "TITLE") == 0) {
                checked_strdup(chart->meta.title,
                    "Multiple TITLE commands, overwritten");
            } else if (strcmp(s, "ARTIST") == 0) {
                checked_strdup(chart->meta.artist,
                    "Multiple ARTIST commands, overwritten");
            } else if (strcmp(s, "SUBARTIST") == 0) {
                checked_strdup(chart->meta.subartist,
                    "Multiple SUBARTIST commands, overwritten");
            } else if (strcmp(s, "BPM") == 0) {
                checked_parse_int(chart->meta.init_tempo,
                    1, 999,
                    "Multiple BPM commands, overwritten");
            } else if (strcmp(s, "PLAYLEVEL") == 0) {
                checked_parse_int(chart->meta.play_level,
                    1, 999,
                    "Multiple PLAYLEVEL commands, overwritten");
            } else if (strcmp(s, "RANK") == 0) {
                checked_parse_int(chart->meta.judge_rank,
                    0, 3,
                    "Multiple RANK commands, overwritten");
            } else if (strcmp(s, "TOTAL") == 0) {
                checked_parse_int(chart->meta.gauge_total,
                    1, 999,
                    "Multiple TOTAL commands, overwritten");
            } else if (strcmp(s, "DIFFICULTY") == 0) {
                checked_parse_int(chart->meta.difficulty,
                    1, 5,
                    "Multiple DIFFICULTY commands, overwritten");
            } else if (strcmp(s, "STAGEFILE") == 0) {
                checked_strdup(chart->meta.stage_file,
                    "Multiple STAGEFILE commands, overwritten");
            } else if (strcmp(s, "BANNER") == 0) {
                checked_strdup(chart->meta.banner,
                    "Multiple BANNER commands, overwritten");
            } else if (strcmp(s, "BACKBMP") == 0) {
                checked_strdup(chart->meta.back_bmp,
                    "Multiple BACKBMP commands, overwritten");
            } else if (memcmp(s, "WAV", 3) == 0 && isbase36(s[3]) && isbase36(s[4])) {
                int index = base36(s[3], s[4]);
                checked_strdup(chart->tables.wav[index],
                    "Wave %c%c specified multiple times, overwritten", s[3], s[4]);
            } else if (memcmp(s, "BMP", 3) == 0 && isbase36(s[3]) && isbase36(s[4])) {
                int index = base36(s[3], s[4]);
                checked_strdup(chart->tables.bmp[index],
                    "Bitmap %c%c specified multiple times, overwritten", s[3], s[4]);
            } else if (memcmp(s, "BPM", 3) == 0 && isbase36(s[3]) && isbase36(s[4])) {
                int index = base36(s[3], s[4]);
                checked_parse_float(chart->tables.tempo[index],
                    1.0, 999.0,
                    "Tempo %c%c specified multiple times, overwritten", s[3], s[4]);
            } else if (memcmp(s, "STOP", 4) == 0 && isbase36(s[4]) && isbase36(s[5])) {
                int index = base36(s[4], s[5]);
                checked_parse_int(chart->tables.stop[index],
                    0, 32767,
                    "Stop %c%c specified multiple times, overwritten", s[4], s[5]);
            } else if (strcmp(s, "LNOBJ") == 0) {
                if (isbase36(s[arg]) && isbase36(s[arg + 1])) {
                    if (lnobj != -1)
                        emit_log(line, "Multiple LNOBJ commands, overwritten");
                    lnobj = base36(s[arg], s[arg + 1]);
                } else {
                    emit_log(line, "Invalid base-36 index %c%c, ignoring",
                        s[arg], s[arg + 1]);
                }
            } else {
                emit_log(line, "Unrecognized command %s, ignoring", s);
            }
        }
    }

    // Postprocessing

    // Reinterpret base-36 as base-16
    for (int i = 0; i < chart->tracks.tempo.note_count; i++) {
        int x = chart->tracks.tempo.notes[i].value;
        chart->tracks.tempo.notes[i].value = (x / 36) * 16 + (x % 36);
    }

    // Sort notes and handle coincident overwrites
    for (int i = 0; i < 60; i++) sort_track(&chart->tracks.fixed[i]);
    sort_track(&chart->tracks.tempo);
    sort_track(&chart->tracks.bga_base);
    sort_track(&chart->tracks.bga_layer);
    sort_track(&chart->tracks.bga_poor);
    sort_track(&chart->tracks.ex_tempo);
    sort_track(&chart->tracks.stop);

    // Handle long notes
    // NOTE: #LNTYPE is not supported and is fixed to LNTYPE 1
    for (int i = 0; i < 20; i++)    // Indices 11-29
        for (int j = 1; j < chart->tracks.fixed[i].note_count; j++) {
            if (chart->tracks.fixed[i].notes[j].value == lnobj &&
                chart->tracks.fixed[i].notes[j - 1].value != -1)
            {
                chart->tracks.fixed[i].notes[j].value = -1;
                chart->tracks.fixed[i].notes[j - 1].hold = true;
                j++;
            }
        }
    for (int i = 40; i < 60; i++)   // Indices 51-69
        for (int j = 1; j < chart->tracks.fixed[i].note_count; j++) {
            if (chart->tracks.fixed[i].notes[j].value ==
                chart->tracks.fixed[i].notes[j - 1].value)
            {
                chart->tracks.fixed[i].notes[j].value = -1;
                chart->tracks.fixed[i].notes[j - 1].hold = true;
                j++;
            }
        }

    #define check_default(_var, _name, _initial, _val) do { \
        if ((_var) == (_initial)) { \
            emit_log(-1, "Command " _name " did not appear, defaulting to " #_val); \
            (_var) = (_val); \
        } \
    } while (0)

    check_default(chart->meta.player_num, "PLAYER", -1, 1);
    check_default(chart->meta.genre, "GENRE", NULL, "(unknown)");
    check_default(chart->meta.title, "TITLE", NULL, "(unknown)");
    check_default(chart->meta.artist, "ARTIST", NULL, "(unknown)");
    check_default(chart->meta.subartist, "SUBARTIST", NULL, "(unknown)");
    check_default(chart->meta.init_tempo, "BPM", -1, 130);
    check_default(chart->meta.play_level, "LEVEL", -1, 3);
    check_default(chart->meta.judge_rank, "RANK", -1, 3);
    check_default(chart->meta.gauge_total, "TOTAL", -1, 160);
    check_default(chart->meta.stage_file, "STAGEFILE", NULL, "(none)");
    check_default(chart->meta.banner, "BANNER", NULL, "(none)");
    check_default(chart->meta.back_bmp, "BACKBMP", NULL, "(none)");

    free(source);
    return log_ptr;
}
