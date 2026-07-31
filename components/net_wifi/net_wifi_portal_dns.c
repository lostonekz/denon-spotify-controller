/*
 * Minimal DNS hijack responder: replies the AP's own IP to every query
 * while the SoftAP portal is active. This -- not the HTML page itself --
 * is what makes a phone's OS notice the network needs "signing in to" and
 * offer to open the captive portal automatically.
 *
 * Deliberately narrow: parses just enough of the query (header + the
 * first question) to build a matching answer; ignores any additional
 * records (e.g. an EDNS OPT pseudo-RR) rather than trying to relocate
 * them after the answer section.
 */
#include "net_wifi_internal.h"

#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"

static const char *TAG = "net_wifi_portal_dns";

#define DNS_PORT       53
#define DNS_MAX_PACKET 512

/* Header (12 bytes) + the first question (labels...0x00 + QTYPE + QCLASS).
 * Returns 0 if the query is too short or a label looks like a compression
 * pointer (0xC0 bit pattern) this minimal parser doesn't handle. */
static size_t question_length(const uint8_t *query, size_t query_len)
{
    size_t pos = 12;
    while (pos < query_len && query[pos] != 0) {
        uint8_t label_len = query[pos];
        if (label_len > 63) {
            return 0;
        }
        pos += (size_t)label_len + 1;
    }
    if (pos >= query_len) {
        return 0;
    }
    pos += 1;   /* terminating 0x00 */
    pos += 4;   /* QTYPE + QCLASS */
    if (pos > query_len) {
        return 0;
    }
    return pos;
}

static size_t build_response(const uint8_t *query, size_t query_len, uint32_t ip_net_order,
                                uint8_t *out, size_t out_cap)
{
    size_t qlen = question_length(query, query_len);
    if (qlen == 0 || qlen + 16 > out_cap) {
        return 0;
    }

    memcpy(out, query, qlen);

    uint8_t rd_bit = query[2] & 0x01;
    out[2]  = (uint8_t)(0x84 | rd_bit);   /* QR=1, Opcode=0, AA=1, RD=echoed */
    out[3]  = 0x00;                       /* RA=0, Z=0, RCODE=0 */
    out[6]  = 0x00; out[7]  = 0x01;       /* ANCOUNT = 1 */
    out[8]  = 0x00; out[9]  = 0x00;       /* NSCOUNT = 0 */
    out[10] = 0x00; out[11] = 0x00;       /* ARCOUNT = 0 */

    size_t pos = qlen;
    out[pos++] = 0xC0; out[pos++] = 0x0C;                 /* NAME: pointer to offset 12 */
    out[pos++] = 0x00; out[pos++] = 0x01;                 /* TYPE = A */
    out[pos++] = 0x00; out[pos++] = 0x01;                 /* CLASS = IN */
    out[pos++] = 0x00; out[pos++] = 0x00; out[pos++] = 0x00; out[pos++] = 0x3C;  /* TTL = 60s */
    out[pos++] = 0x00; out[pos++] = 0x04;                 /* RDLENGTH = 4 */
    memcpy(&out[pos], &ip_net_order, 4);
    pos += 4;

    return pos;
}

static void dns_task(void *arg)
{
    net_wifi_t *w = (net_wifi_t *)arg;

    for (;;) {
        uint8_t query[DNS_MAX_PACKET];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);

        int n = recvfrom(w->dns_socket, query, sizeof(query), 0, (struct sockaddr *)&from, &from_len);
        if (n <= 0) {
            if (w->dns_socket < 0) {
                break;   /* net_wifi_portal_dns_stop() closed us intentionally */
            }
            continue;
        }

        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(w->ap_netif, &ip_info) != ESP_OK) {
            continue;
        }

        uint8_t response[DNS_MAX_PACKET];
        size_t response_len = build_response(query, (size_t)n, ip_info.ip.addr,
                                                response, sizeof(response));
        if (response_len > 0) {
            sendto(w->dns_socket, response, response_len, 0, (struct sockaddr *)&from, from_len);
        }
    }

    vTaskDelete(NULL);
}

void net_wifi_portal_dns_start(net_wifi_t *w)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        w->last_action_ok = false;
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(DNS_PORT);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind(:53) failed");
        close(sock);
        w->last_action_ok = false;
        return;
    }

    w->dns_socket = sock;

    BaseType_t ok = xTaskCreate(dns_task, "net_wifi_dns", 3072, w, 4, &w->dns_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create DNS task");
        close(sock);
        w->dns_socket = -1;
        w->last_action_ok = false;
        return;
    }

    w->last_action_ok = true;
}

void net_wifi_portal_dns_stop(net_wifi_t *w)
{
    if (w->dns_socket >= 0) {
        int fd = w->dns_socket;
        w->dns_socket = -1;   /* checked by dns_task to distinguish an intentional close */
        close(fd);
    }
}
