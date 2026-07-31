#include "net_wifi_ap_password.h"

static char hex_digit(uint8_t nibble)
{
    return (nibble < 10) ? (char)('0' + nibble) : (char)('a' + (nibble - 10));
}

void net_wifi_ap_password_from_mac(const uint8_t mac[6], char out[NET_WIFI_AP_PASSWORD_MAX])
{
    out[0] = 'd';
    out[1] = 's';
    out[2] = 'c';

    int pos = 3;
    for (int i = 3; i < 6; i++) {
        out[pos++] = hex_digit((uint8_t)(mac[i] >> 4));
        out[pos++] = hex_digit((uint8_t)(mac[i] & 0x0F));
    }
    out[pos] = '\0';
}
