/* Pure selection policy shared by the plugin and native-host tests. */
#ifndef VC_OUTFIT_SELECT_H
#define VC_OUTFIT_SELECT_H
#include <stddef.h>
#include <string.h>
#include "known_scenes.h"

/* Bounded ASCII comparison: unknown/unterminated names never become eligible. */
static int vc_ascii_equal(const char *a, const char *b, size_t limit) {
    if (!a || !b) return 0;
    for (size_t i = 0; i < limit; i++) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x += 'a'-'A';
        if (y >= 'A' && y <= 'Z') y += 'a'-'A';
        if (x != y) return 0;
        if (!x) return 1;
    }
    return 0;
}
static int vc_known_scene(const char *name) {
    for (size_t i=0; i<sizeof(vc_known_scenes)/sizeof(vc_known_scenes[0]); i++)
        if (vc_ascii_equal(name, vc_known_scenes[i], 12)) return 1;
    return 0;
}
static const char *vc_outfit_model(int drawable) {
    static const char *const models[] = {
        NULL, NULL, "cstommy3", "cstommy4", "cstommy5", "cstommy6", "cstommy7",
        "cstommy8", "cstommy9", "cstommy10", "cstommy11", "cstommy12", "cstommy13", "cstommy14"
    };
    if (drawable < 2 || drawable >= (int)(sizeof(models)/sizeof(models[0]))) return NULL;
    return models[drawable];
}
static const char *vc_replace_tommy(const char *token, int drawable) {
    const char *model = vc_outfit_model(drawable);
    if (!model) return NULL;
    if (vc_ascii_equal(token,"cstommy2_b",12)) {
        static const char *const wounded[] = {
            NULL, NULL, "cstommy3_b", "cstommy4_b", "cstommy5_b", "cstommy6_b",
            "cstommy7_b", "cstommy8_b", "cstommy9_b", "cstommy10_b", "cstommy11_b",
            "cstommy12_b", "cstommy13_b", "cstommy14_b"
        };
        return wounded[drawable];
    }
    if (vc_ascii_equal(token,"cstommy",12) || vc_ascii_equal(token,"cstommy2",12)) return model;
    return NULL;
}
#endif
