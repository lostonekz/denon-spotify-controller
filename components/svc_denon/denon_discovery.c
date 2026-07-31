/*
 * SSDP discovery. See denon_discovery.h for the KNOWN GAP note on the
 * broad-search-then-filter approach used here. Target-only -- raw UDP
 * multicast via lwIP sockets, not host-testable.
 */
#include "denon_discovery.h"

#include <ctype.h>
#include <string.h>

#include "lwip/sockets.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "denon_discovery";

#define SSDP_MCAST_ADDR "239.255.255.250"
#define SSDP_MCAST_PORT 1900
#define SSDP_RESP_BUF_LEN 512u

static const char SSDP_MSEARCH[] =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 2\r\n"
    "ST: ssdp:all\r\n"
    "\r\n";

/* No _GNU_SOURCE-only strcasestr dependency -- a small portable
 * case-insensitive substring search instead. */
static bool contains_ci(const char *haystack, size_t haystack_len, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || haystack_len < needle_len) {
        return false;
    }
    for (size_t i = 0; i + needle_len <= haystack_len; i++) {
        size_t j = 0;
        for (; j < needle_len; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                break;
            }
        }
        if (j == needle_len) {
            return true;
        }
    }
    return false;
}

bool denon_discovery_run(char *out_host, size_t out_cap, uint32_t timeout_ms)
{
    if (out_host == NULL || out_cap == 0) {
        return false;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        ESP_LOGW(TAG, "socket() failed");
        return false;
    }

    struct sockaddr_in dest = { 0 };
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(SSDP_MCAST_PORT);
    if (inet_pton(AF_INET, SSDP_MCAST_ADDR, &dest.sin_addr) != 1) {
        lwip_close(fd);
        return false;
    }

    if (sendto(fd, SSDP_MSEARCH, sizeof(SSDP_MSEARCH) - 1, 0,
                (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        ESP_LOGW(TAG, "sendto() failed");
        lwip_close(fd);
        return false;
    }

    bool found = false;
    int64_t deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    while (!found) {
        int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0) {
            break;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = {
            .tv_sec  = (long)(remaining_us / 1000000),
            .tv_usec = (long)(remaining_us % 1000000),
        };

        int sel = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (sel <= 0) {
            break;   /* timeout or select error -- no responder found */
        }

        char resp[SSDP_RESP_BUF_LEN];
        struct sockaddr_in from = { 0 };
        socklen_t from_len = sizeof(from);
        int n = recvfrom(fd, resp, sizeof(resp) - 1, 0, (struct sockaddr *)&from, &from_len);
        if (n <= 0) {
            continue;
        }
        resp[n] = '\0';

        if (contains_ci(resp, (size_t)n, "denon")) {
            if (inet_ntop(AF_INET, &from.sin_addr, out_host, (socklen_t)out_cap) != NULL) {
                found = true;
            }
        }
        /* Not a Denon response -- keep listening until the deadline; some
         * other device on the LAN answered the broad ssdp:all search. */
    }

    lwip_close(fd);
    return found;
}
