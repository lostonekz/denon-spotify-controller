/*
 * Entry point. docs/MODULES.md: main.c does nothing but hand off. All
 * wiring lives in the composition root (app_wiring.c); all policy lives in
 * components.
 */

#include "app_start.h"

void app_main(void)
{
    app_start();
}
