/*
 * Telnet transport. See denon_telnet.h / denon_transport.h.
 * Target-only -- ESP-IDF/lwIP sockets, not host-testable.
 */
#include "denon_telnet.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "esp_log.h"

static const char *TAG = "denon_telnet";

#define DENON_TELNET_CONNECT_TIMEOUT_S 3

static bool set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool denon_telnet_open(void *vctx, const char *host)
{
    denon_telnet_ctx_t *ctx = (denon_telnet_ctx_t *)vctx;

    /* Real bug, found on real hardware: this used to assign ctx->fd = -1
     * directly, which ORPHANS a still-open socket instead of closing it.
     *
     * That leaked one descriptor per reconnect, and this link reconnects
     * constantly by design: TELNET_EOF falls straight through to
     * CONNECTING_HTTP (adr/0003 point 2), and the only action that closes
     * the transports -- action_release_link() -- is not on that path, so
     * the telnet socket stayed open until the next promotion attempt came
     * back here and overwrote the handle. Against a real AVR-X3800H whose
     * HEOS firmware drops the telnet connection every ~10s (the documented
     * "telnet goes unresponsive" behaviour this project already works
     * around elsewhere), that is a leak every 10 seconds.
     *
     * Measured: first `esp-tls: Failed to create socket` at 176s of uptime,
     * followed by 57 more and total loss of networking -- discovery,
     * artwork and Spotify all fail once LWIP's socket table is full, and
     * the panel can no longer even power the receiver on. It presents as
     * "it worked when I turned it on and got worse over time", because the
     * leak is purely a function of uptime.
     *
     * Closing here rather than only on the EOF path is deliberate: this is
     * the one function that can clobber the handle, so guarding it makes
     * the leak unreachable no matter which FSM route led here. */
    if (ctx->fd >= 0) {
        lwip_close(ctx->fd);
        ctx->fd = -1;
    }
    ctx->rxlen = 0;

    if (host == NULL) {
        return false;
    }

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res  = NULL;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", DENON_TELNET_PORT);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL) {
        ESP_LOGW(TAG, "getaddrinfo(%s) failed", host);
        return false;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return false;
    }
    if (!set_nonblocking(fd)) {
        lwip_close(fd);
        freeaddrinfo(res);
        return false;
    }

    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (rc < 0 && errno != EINPROGRESS) {
        ESP_LOGW(TAG, "connect(%s) failed: errno=%d", host, errno);
        lwip_close(fd);
        return false;
    }

    if (rc < 0) {
        /* EINPROGRESS -- wait for the connect to actually complete, bounded. */
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = { .tv_sec = DENON_TELNET_CONNECT_TIMEOUT_S, .tv_usec = 0 };

        int sel = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (sel <= 0) {
            ESP_LOGW(TAG, "connect(%s) timed out", host);
            lwip_close(fd);
            return false;
        }
        int       so_error = 0;
        socklen_t len       = sizeof(so_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 || so_error != 0) {
            ESP_LOGW(TAG, "connect(%s) refused: so_error=%d", host, so_error);
            lwip_close(fd);
            return false;
        }
    }

    ctx->fd = fd;
    return true;
}

static bool denon_telnet_send_cmd(void *vctx, const char *cmd)
{
    denon_telnet_ctx_t *ctx = (denon_telnet_ctx_t *)vctx;
    if (ctx->fd < 0 || cmd == NULL) {
        return false;
    }

    /* Raw ASCII, \r-terminated -- NETWORKING.md §1.4. */
    char line[DENON_TRANSPORT_LINE_BUF_LEN + 1];
    int n = snprintf(line, sizeof(line), "%s\r", cmd);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return false;   /* cmd too long -- caller bug, not a transport failure */
    }

    /* A single send() for a command this small (<=32B) either fully
     * succeeds on a healthy link or the link is in trouble -- no partial-
     * send retry loop here; a failed command gets superseded (absolute
     * classes) or the next poll() surfaces the dead link either way. */
    ssize_t sent = send(ctx->fd, line, (size_t)n, 0);
    return sent == n;
}

static bool try_extract_line(denon_telnet_ctx_t *ctx, char *out_line, size_t out_cap)
{
    for (size_t i = 0; i < ctx->rxlen; i++) {
        if (ctx->rxbuf[i] != '\r' && ctx->rxbuf[i] != '\n') {
            continue;
        }

        size_t line_len = i;
        if (line_len >= out_cap) {
            line_len = out_cap - 1;   /* truncate defensively -- never overflow the caller's buffer */
        }
        memcpy(out_line, ctx->rxbuf, line_len);
        out_line[line_len] = '\0';

        /* Treat a \r\n or \n\r pair as one terminator, not an empty line. */
        size_t consumed = i + 1;
        if (consumed < ctx->rxlen &&
            ((ctx->rxbuf[i] == '\r' && ctx->rxbuf[consumed] == '\n') ||
             (ctx->rxbuf[i] == '\n' && ctx->rxbuf[consumed] == '\r'))) {
            consumed++;
        }
        memmove(ctx->rxbuf, ctx->rxbuf + consumed, ctx->rxlen - consumed);
        ctx->rxlen -= consumed;
        return true;
    }
    return false;
}

static denon_transport_poll_result_t denon_telnet_poll(void *vctx, char *out_line,
                                                          size_t out_cap, uint32_t timeout_ms)
{
    denon_telnet_ctx_t *ctx = (denon_telnet_ctx_t *)vctx;
    if (ctx->fd < 0 || out_line == NULL || out_cap == 0) {
        return DENON_TRANSPORT_POLL_LOST;
    }

    /* Drain whatever a previous recv() already buffered before blocking for more. */
    if (try_extract_line(ctx, out_line, out_cap)) {
        return DENON_TRANSPORT_POLL_LINE;
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(ctx->fd, &rfds);
    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };

    int sel = select(ctx->fd + 1, &rfds, NULL, NULL, &tv);
    if (sel == 0) {
        return DENON_TRANSPORT_POLL_NOTHING;   /* plain timeout -- link still fine */
    }
    if (sel < 0) {
        return DENON_TRANSPORT_POLL_LOST;
    }

    if (ctx->rxlen >= DENON_TELNET_RXBUF_LEN) {
        /* Full buffer with no terminator found at all -- a runaway/malformed
         * stream. Drop it and start fresh rather than wedging forever. */
        ctx->rxlen = 0;
    }

    ssize_t n = recv(ctx->fd, ctx->rxbuf + ctx->rxlen, DENON_TELNET_RXBUF_LEN - ctx->rxlen, 0);
    if (n == 0) {
        return DENON_TRANSPORT_POLL_LOST;   /* peer closed -- EOF */
    }
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? DENON_TRANSPORT_POLL_NOTHING
                                                            : DENON_TRANSPORT_POLL_LOST;
    }
    ctx->rxlen += (size_t)n;

    return try_extract_line(ctx, out_line, out_cap) ? DENON_TRANSPORT_POLL_LINE
                                                        : DENON_TRANSPORT_POLL_NOTHING;
}

static void denon_telnet_close(void *vctx)
{
    denon_telnet_ctx_t *ctx = (denon_telnet_ctx_t *)vctx;
    if (ctx->fd >= 0) {
        lwip_close(ctx->fd);
    }
    ctx->fd    = -1;
    ctx->rxlen = 0;
}

denon_transport_if_t denon_telnet_transport(denon_telnet_ctx_t *ctx)
{
    ctx->fd    = -1;
    ctx->rxlen = 0;

    denon_transport_if_t iface = {
        .open      = denon_telnet_open,
        .send_cmd  = denon_telnet_send_cmd,
        .poll      = denon_telnet_poll,
        .close     = denon_telnet_close,
        .ctx       = ctx,
    };
    return iface;
}
