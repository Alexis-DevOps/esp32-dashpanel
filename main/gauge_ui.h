#pragma once

/** Show the logo splash on the active screen. Call with the LVGL lock held. */
void gauge_ui_show_splash(void);

/** Build the round RPM / speed / gear gauge on the active screen. Call with the LVGL lock held. */
void gauge_ui_create(void);
