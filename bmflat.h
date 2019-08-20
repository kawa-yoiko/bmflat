#ifndef _BMFLAT_H_
#define _BMFLAT_H_

#ifdef __cplusplus
extern "C" {
#endif

struct bm_metadata {
    char *genre;
    char *title;
    char *artist;
    char *subartist;
};

struct bm_chart {
    struct bm_metadata meta;
};

#define BM_MSG_LEN  64

struct bm_log {
    int line;
    char message[BM_MSG_LEN];
};

extern struct bm_log *bm_logs;

int bm_load(struct bm_chart *chart, const char *source);

#ifdef __cplusplus
}
#endif

#endif
