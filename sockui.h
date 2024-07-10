#pragma once
#include <stdbool.h>
#include <wchar.h>
#include <stdint.h>

#define SOCKUI_IBUF_LEN 32
#define SOCKUI_TMPBUF_LEN 128

typedef struct SockUI {
    int port;
    void *priv;
    bool (*resize)(int dim[2], void* priv);
    bool (*draw)(int dim[2], wchar_t *str);

    bool should_redraw;
    int serv_fd;
    int client_fd;

    int ibuf_idx;
    int ibuf_cap;
    uint8_t ibuf[SOCKUI_IBUF_LEN];
    uint8_t tmpbuf[SOCKUI_TMPBUF_LEN];
} SockUI;

int sockui_init(SockUI *sockui);
