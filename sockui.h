#pragma once
#include <stdbool.h>
#include <wchar.h>
#include <stdint.h>

#define SOCKUI_IBUF_LEN 32
#define SOCKUI_TMPBUF_LEN 128

/**
 * Error type for this library. Always negative.
 * SOCKUI_ESYS: A syscall returned an error and errno is set
 * SOCKUI_EILSEQ: There was an illegal unicode sequence in an input
 * SOCKUI_EIO: Failed to read or write to a socket
 */
typedef enum { SOCKUI_ESYS = -1, SOCKUI_EILSEQ = -2, SOCKUI_EIO = -3 } sockui_err_t;

typedef struct sockui_s {
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
} sockui_t;

int sockui_init(sockui_t *sockui);
int sockui_init(sockui_t *sui);
int sockui_recv(sockui_t *sui);
int sockui_draw_menu(sockui_t *sui, wchar_t *menu, int dim[2]);
void sockui_attach_client(sockui_t *sui, int client_fd);
void sockui_close(sockui_t *sui);
bool sockui_get_size(sockui_t *sui, int dim[2]);
char *sockui_strerror(sockui_err_t e);
