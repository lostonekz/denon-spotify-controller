/*
 * spotify_api_result_t alone, split out of spotify_api.h (Phase 5 Tier -1)
 * so a pure module (spotify_intent_ack.h) can share this type without
 * dragging in spotify_api.h's own net_http.h dependency -- net_http.h
 * includes freertos/FreeRTOS.h, which would make anything that transitively
 * needs just this enum target-only for no real reason.
 */
#ifndef SPOTIFY_API_RESULT_H
#define SPOTIFY_API_RESULT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPOTIFY_API_RESULT_OK = 0,
    SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE,   /* 204 -- valid, not an error */
    SPOTIFY_API_RESULT_UNAUTHORIZED,       /* 401 -- access token expired/invalid; caller should refresh */
    SPOTIFY_API_RESULT_FAILED,
} spotify_api_result_t;

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_API_RESULT_H */
