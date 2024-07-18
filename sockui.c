#include "sockui.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/socket.h>

/// Simple wrapper for writing string literals
#define emit(fd, s) write(fd, s, sizeof(s));

/// Shortcut macro; should remove after clarifying error API
#define IF_ERR_RETURN(cond) do { int __x; if ((__x = (cond)) < 0) return __x; } while (0);

/**
 * Read as much from a socket as possible.
 *
 * @fd Socket file descriptor, must be SOCK_NONBLOCK
 * @buf Buffer of bytes to read into. Passing a null pointer empties the socket
 *      without saving any data
 * @nbytes Length of buf. Ignored when buf == NULL
 * @return Number of bytes read, or sockui_err_t on error, with errno set appropriately
 */
static ssize_t sock_read(int fd, void *buf, size_t nbytes) {
    ssize_t ret, total=0;
    if (!buf) {
        uint64_t c;
        while((ret = read(fd, &c, sizeof(c)))) {
            if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            else if (ret == -1) return SOCKUI_ESYS;

            total += ret;
        }

        return total;
    }

    if (!nbytes) return 0;
    while((ret = read(fd, buf + total, nbytes - total))) {
        if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        else if (ret == -1) return SOCKUI_ESYS;

        total += ret;
    }

    return total;
}

/**
 * Wrapper for write socket writing that assumes all bytes must be sent OR ELSE
 *
 * @fd Socket file descriptor, must be SOCK_NONBLOCK
 * @buf Buffer of bytes to write from.
 * @nbytes Length of buf
 * @return 0 on success, sockui_err_t on error. TODO: Make this return num written bytes?
 */
static ssize_t sock_write(int fd, void *buf, size_t nbytes) {
    ssize_t ret = write(fd, buf, nbytes);
    if (ret == -1) return SOCKUI_ESYS;
    else if (ret != (ssize_t) nbytes) return SOCKUI_EIO;

    return 0;
}

/**
 * Boilerplate for opening, binding, and listening a new socket
 *
 * @port The port to open the socket on
 * @return The socket fd or sockui_err_t on error
 */
static int new_sock(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return SOCKUI_ESYS;

    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int) { 1 }, sizeof(int));
    if (ret)
        goto err;

    struct sockaddr_in sock_addr = { 0 };
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    sock_addr.sin_port = htons(port);
    ret = bind(fd, (struct sockaddr *) &sock_addr, sizeof(sock_addr));
    if (ret)
        goto err;

    ret = listen(fd, 0);
    if (ret)
        goto err;

    return fd;

err:
    close(fd);
    return SOCKUI_ESYS;
}

/**
 * Converts a Unicode wchar_t* string to UTF-8. Bails out on invalid input.
 * No results of this function are valid if it returns -2
 *
 * @str Unicode string to convert
 * @nstr Length of str
 * @buf Buffer to write UTF-8 into
 * @nbuf Length of buf
 * @nbytes Number of bytes written into buf
 * @return Number of CODEPOINTS written to buf or -2 on error
 */
static int unicode_to_utf8(wchar_t *str, int nstr, uint8_t *buf, int nbuf, int *nbytes) {
    if (!str || !nstr) return 0;
    uint8_t *orig = buf;

    int i;
    wchar_t c;
    for (i = 0; i < nstr; i++) {
        c = str[i];
        if (c<0x80) {
            if (nbuf < 1) goto out;
            *buf++=c;
            nbuf -= 1;
        } else if (c<0x800) {
            if (nbuf < 2) goto out;
            *buf++=192+c/64, *buf++=128+c%64;
            nbuf -= 2;
        } else if (c-0xd800u<0x800) {
            goto eilseq;
        } else if (c<0x10000) {
            if (nbuf < 2) goto out;
            *buf++=224+c/4096, *buf++=128+c/64%64, *buf++=128+c%64;
            nbuf -= 3;
        } else if (c<0x110000) {
            if (nbuf < 2) goto out;
            *buf++=240+c/262144, *buf++=128+c/4096%64, *buf++=128+c/64%64, *buf++=128+c%64;
            nbuf -= 4;
        } else {
            goto eilseq;
        }
    }

out:
    *nbytes = buf - orig;
    return i;
eilseq:
    return SOCKUI_EILSEQ;
}

/**
 * Gets the byte offset for a given number of codepoints in a UTF-8 string.
 * Assumes there are enough codepoints in the buffer for this operation to be
 * valid
 *
 * @buf A UTF-8 valid string of at least $count codepoints
 * @count Number of codepoints to count for offset
 * @return The offset of the codepoint byte after $count codepoints
 */
static int utf8_offset(uint8_t *buf, int count) {
    if (!count) return 0;

    int offset = 0;
    while (count > 0)
        if ((buf[offset++] & 0xc0) != 0x80) count--;

    if (buf[offset-1] & 0xC0) offset += 2;
    else if (buf[offset-1] & 0xE0) offset += 3;
    else if (buf[offset-1] & 0xF0) offset += 4;

    return offset;
}

/**
 * Returns an appropriate string describing the given error code.
 * Passes through to regular strerror if @e is SOCKUI_ESYS
 *
 * @e The error to fetch a string for
 * @return A non-null string describing @e
 */
char *sockui_strerror(sockui_err_t e) {
    switch (e) {
        case SOCKUI_ESYS:
            return strerror(errno);
        case SOCKUI_EILSEQ:
            return "Illegal Unicode sequence";
        case SOCKUI_EIO:
            return "Failed to read or write socket";
        default:
            return "Unknown error code";
    }
}

/**
 * Initialize a SockUI object
 *
 * @sui A valid pointer to a SockUI object with port initialized
 * @return 0 on success, sockui_err_t on error
 */
int sockui_init(sockui_t *sui) {
    sui->client_fd = -1;
    sui->serv_fd = new_sock(sui->port);
    if (sui->serv_fd < 0)
        return sui->serv_fd;

    sui->ibuf_idx = 0;
    sui->ibuf_cap = 0;
    sui->should_redraw = false;

    return 0;
}

/**
 * Uses ANSI escape codes to get the size of the terminal on the other end of
 * the socket. TODO: Make this a public API function and figure out a consisten
 * error API
 *
 * @sui A valid SockUI object
 * @dim Result of getting terminal size. Rows, Cols
 * @return Bool indicating success
 */
bool sockui_get_size(sockui_t *sui, int dim[2]) {
    if (sock_read(sui->client_fd, NULL, 0) == -1)
        return false;

    emit(sui->client_fd, "\033[s\033[9999;9999H\033[6n\033[u");
    usleep(100*1000);

    // sui->tmpbuf is definitely big enough
    if (sock_read(sui->client_fd, sui->tmpbuf, sizeof(sui->tmpbuf)) == -1)
        return false;

    sui->tmpbuf[sizeof(sui->tmpbuf)-1] = '\0';
    if (sscanf((char *) sui->tmpbuf, "\033[%d;%dR", dim, dim+1) != 2)
        return false;

    return true;
}


/**
 * Receive input from a SockUI object. Any bytes received over the socket that
 * are not considered SockUI control bytes are returned to the user. Control
 * bytes include: ^L.
 *
 * @sui A valid pointer to a SockUI object
 * @return A single byte on success, 256 if no bytes available, or sockui_err_t
 */
int sockui_recv(sockui_t *sui) {
    int ret;
    bool is_ctrl_code;
    uint8_t b = 0;

    do {
        is_ctrl_code = false;
        if (sui->ibuf_cap == sui->ibuf_idx) {
            sui->ibuf_cap = sock_read(sui->client_fd, sui->ibuf, sizeof(sui->ibuf));
            if (!sui->ibuf_cap) return 256;
            if (sui->ibuf_cap == -1) {
                ret = SOCKUI_ESYS;
                goto reset;
            }
            sui->ibuf_idx = 0;
        }

        b = sui->ibuf[sui->ibuf_idx++];
        if (b == 0x0c) { // ^L
            is_ctrl_code = true;
            sui->should_redraw = true;
            emit(sui->client_fd, "\033[2J");
        }
    } while (is_ctrl_code);

    return b;

reset:
    sui->ibuf_cap = 0;
    sui->ibuf_idx = 0;
    return ret;
}

/**
 * Wrapper for drawing menus over a SockUI. Menu strings should not contain
 * newlines or carriage returns. These will be inserted after each row.
 *
 * @sui A valid pointer to a SockUI object
 * @menu A valid Unicode string representing the menu
 * @dim The dimensions of $menu. Rows, Cols
 * @return 0 on success, sockui_err_t on error
 */
int sockui_draw_menu(sockui_t *sui, wchar_t *menu, int dim[2]) {
    int nstr = dim[0]*dim[1];
    int nbuf = sizeof(sui->tmpbuf);

    emit(sui->client_fd, "\033[2J");
    emit(sui->client_fd, "\033[0;0H");

    int total = 0, n, nbytes;
    while ((n = unicode_to_utf8(menu+total, nstr-total, sui->tmpbuf, nbuf, &nbytes))) {
        if (n < 0) return n;

        uint8_t *tmpbuf = sui->tmpbuf;
        do {
            int until_nl = dim[1] - total%dim[1];
            if (n >= until_nl) {
                int offset = utf8_offset(tmpbuf, until_nl);
                IF_ERR_RETURN(sock_write(sui->client_fd, tmpbuf, offset));
                IF_ERR_RETURN(sock_write(sui->client_fd, "\r\n", 2));
                tmpbuf += offset, nbytes -= offset;
                total += until_nl, n -= until_nl;
            } else {
                IF_ERR_RETURN(sock_write(sui->client_fd, tmpbuf, nbytes));
                total += n, n = 0;
            }
        } while (n > 0);
    }

    return 0;
}

/**
 * Initialize the client's terminal after socket accept, and set the SockUI's
 * client_fd
 *
 * @sui A valid pointer to a SockUI object
 * @client_fd File descriptor of the accepted client
 */
void sockui_attach_client(sockui_t *sui, int client_fd) {
    sui->client_fd = client_fd;
    emit(sui->client_fd, "\033[?1049h");
    emit(sui->client_fd, "\033[2J");
    emit(sui->client_fd, "\033[0;0H");
    emit(sui->client_fd, "\033[?25l");
}

/**
 * Deinitialize the client's terminal and close server and client sockets
 *
 * @sui A valid pointer to a SockUI object
 */
void sockui_close(sockui_t *sui) {
    emit(sui->client_fd, "\033[?2J");
    emit(sui->client_fd, "\033[?1049l");
    emit(sui->client_fd, "\033[?25h");
    close(sui->client_fd);
    close(sui->serv_fd);
}

