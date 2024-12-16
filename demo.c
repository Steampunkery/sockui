#include "sockui.h"

#include <bits/time.h>
#include <bits/types/siginfo_t.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <wait.h>

// Global so signal handler can find it
sockui_t sui = {
    .port = 6969,
    .client_fd = -1,
    .serv_fd = -1,
};

void exit_handler(int sig) {
    (void) sig;
    if (sui.client_fd != -1)
        sockui_close(&sui);

    printf("\n"); // Don't care
    _exit(0);
}

int main() {
    signal(SIGINT, exit_handler);
    signal(SIGQUIT, exit_handler);
    signal(SIGPIPE, SIG_IGN);

    // TODO: Determine if socket options should be set manually by the user or
    // otherwise
    int ret = sockui_init(&sui);
    if (ret == -1) {
        fprintf(stderr, "Failed to init SockUI\n");
        _exit(1);
    }

    if (sockui_attach_client(&sui) == -1) {
        fprintf(stderr, "Failed to attach client\n");
        _exit(1);
    }

    int dim[2];
    bool success = sockui_get_size(&sui, dim);
    if (success)
        printf("Rows: %d, Columns: %d\n", dim[0], dim[1]);
    else
        printf("Failed to get terminal size\n");

    wchar_t *menu = wcsdup(L"┌─────────┐│test menu│└─────────┘");
    dim[0] = 3;
    dim[1] = 11;

    struct timespec last, curr;
    clock_gettime(CLOCK_MONOTONIC, &last);

    int i = 0, c;
    sockui_err_t e;
    while (1) {
        if ((c = sockui_recv(&sui)) == 'q') {
            break;
        } else if (c > -1 && c < 256) {
            printf("Received: %c\n", c);
        }

        clock_gettime(CLOCK_MONOTONIC, &curr);
        if ((curr.tv_sec - last.tv_sec) * 1000000000 + (curr.tv_nsec - last.tv_nsec) > 250000000) {
            menu[16] = L'0' + (i++%10);
            if ((e = sockui_draw_menu(&sui, menu, dim)) < 0) {
                fprintf(stderr, "sockui_draw_menu: %s\n", sockui_strerror(e));
                exit_handler(0);
            }
            last = curr;
        }
        usleep(1000);
    }

    exit_handler(0);
    return 0;
}
