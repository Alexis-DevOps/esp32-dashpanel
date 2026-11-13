#include "gauge_ui.h"

#include <stdio.h>

#include "lvgl.h"
#include "digits_img.h"
#include "logo_img.h"
#include "vehicle_data.h"

/* Defaults/proportions come from the original 8000 rpm scale: redline 81%, shift light 6% before it, amber from 56% */
#define RPM_MAX_LIMIT 12000
#define UI_PERIOD_MS 20 /* match LV_DEF_REFR_PERIOD: 50 FPS */
#define SPEED_Y (-34)  /* vertical centre of the speed digits, relative to screen centre */

#define COL_BG lv_color_hex(0x000000)
#define COL_TRACK lv_color_hex(0x1C1C1C)
#define COL_TEXT lv_color_hex(0xFFFFFF)
#define COL_DIM lv_color_hex(0x8A8A8A)
#define COL_RED lv_color_hex(0xFF2D2D)
#define FUEL_LOW_PCT 15
#define FLASH_HALF_PERIOD_MS 100 /* 5 Hz red border flash in the red zone */
#define RING_W 10                /* border ring width, px, drawn at the screen edge */
#define RING_SEGS 36             /* ring is invalidated as small tiles, not the whole screen */

static lv_obj_t *s_arc;
static lv_obj_t *s_scale;
static lv_scale_section_t *s_red_section;
static int s_rpm_max, s_rpm_redline, s_rpm_shift, s_rpm_amber;
static lv_obj_t *s_speed_dig[3]; /* pre-rendered digit images: no font scaling at runtime */
static lv_obj_t *s_fuel_bar;
static lv_obj_t *s_gear_box;
static lv_obj_t *s_gear;
static lv_obj_t *s_kmh; /* "km/h", or NOT CONNECTED while there is no data */
static int s_last_connected = -1;

static float s_rpm_disp;   /* smoothed rpm for the arc */
static int s_last_speed = -1;
static int s_last_ndig = -1;
static int s_last_gear = -99;
static int s_last_fuel = -1;
static uint32_t s_blink;
static int s_last_band = -1;  /* 0 normal, 1 amber, 2 red */
static int s_last_gear_style = -1;
static int s_last_flash = -1;
static lv_obj_t *s_ring;
static bool s_ring_on;
static lv_area_t s_ring_tiles[RING_SEGS];

/* Draws the red border ring when s_ring_on */
static void ring_draw_cb(lv_event_t *e)
{
    if (!s_ring_on) return;
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = COL_RED;
    d.width = RING_W;
    d.center.x = (c.x1 + c.x2 + 1) / 2;
    d.center.y = (c.y1 + c.y2 + 1) / 2;
    d.radius = lv_area_get_width(&c) / 2;
    d.start_angle = 0;
    d.end_angle = 360;
    lv_draw_arc(lv_event_get_layer(e), &d);
}

/* Bounding boxes of RING_SEGS annular sectors: invalidating these redraws only the edge */
static void ring_build_tiles(void)
{
    lv_area_t c;
    lv_obj_update_layout(s_ring);
    lv_obj_get_coords(s_ring, &c);
    int cx = (c.x1 + c.x2 + 1) / 2, cy = (c.y1 + c.y2 + 1) / 2;
    int r_out = lv_area_get_width(&c) / 2, r_in = r_out - RING_W;
    for (int i = 0; i < RING_SEGS; i++) {
        lv_area_t *t = &s_ring_tiles[i];
        t->x1 = t->y1 = INT16_MAX;
        t->x2 = t->y2 = INT16_MIN;
        for (int a = i * 360 / RING_SEGS; a <= (i + 1) * 360 / RING_SEGS; a += 2) {
            int s = lv_trigo_sin(a), co = lv_trigo_cos(a);
            for (int r = r_in; r <= r_out; r += RING_W) {
                int x = cx + ((co * r) >> LV_TRIGO_SHIFT), y = cy + ((s * r) >> LV_TRIGO_SHIFT);
                if (x < t->x1) t->x1 = x;
                if (x > t->x2) t->x2 = x;
                if (y < t->y1) t->y1 = y;
                if (y > t->y2) t->y2 = y;
            }
        }
        lv_area_increase(t, 2, 2); /* rounding + anti-aliasing margin */
    }
}

static void ring_set(bool on)
{
    s_ring_on = on;
    for (int i = 0; i < RING_SEGS; i++) {
        lv_obj_invalidate_area(s_ring, &s_ring_tiles[i]);
    }
}

/* (Re)configure the rev scale for another max rpm / redline. Cheap no-op when unchanged. */
static void apply_rpm_scale(int max, int redline)
{
    if (max <= 0) max = VEHICLE_DEFAULT_RPM_MAX;
    max = ((max + 999) / 1000) * 1000; /* labels are whole thousands */
    if (max > RPM_MAX_LIMIT) max = RPM_MAX_LIMIT;
    if (redline <= 0 || redline > max) redline = max * 13 / 16;
    if (max == s_rpm_max && redline == s_rpm_redline) return;

    static const char *labels[RPM_MAX_LIMIT / 1000 + 2];
    static char label_txt[RPM_MAX_LIMIT / 1000 + 1][3];
    int n = max / 1000;
    for (int i = 0; i <= n; i++) {
        snprintf(label_txt[i], sizeof(label_txt[i]), "%d", i);
        labels[i] = label_txt[i];
    }
    labels[n + 1] = NULL;

    s_rpm_max = max;
    s_rpm_redline = redline;
    s_rpm_shift = redline - max / 16;
    s_rpm_amber = max * 9 / 16;

    lv_arc_set_range(s_arc, 0, max);
    lv_scale_set_range(s_scale, 0, max);
    lv_scale_set_total_tick_count(s_scale, n * 4 + 1); /* every 250 rpm */
    lv_scale_set_text_src(s_scale, labels);
    lv_scale_set_section_range(s_scale, s_red_section, redline, max);
    s_last_band = -1; /* thresholds moved: re-evaluate the arc colour */
}

static int rpm_band(int rpm)
{
    if (rpm >= s_rpm_redline) return 2;
    if (rpm >= s_rpm_amber) return 1;
    return 0;
}

/* Only the digit images whose value changed get redrawn */
static void set_speed(int v)
{
    if (v < 0) v = 0;
    if (v > 999) v = 999;
    int ndig = v >= 100 ? 3 : v >= 10 ? 2 : 1;
    if (ndig != s_last_ndig) {
        /* centre the visible digits; only happens when crossing 10 or 100 km/h */
        int x0 = -(ndig * SPEED_DIGIT_W) / 2 + SPEED_DIGIT_W / 2;
        for (int i = 0; i < 3; i++) {
            if (i < ndig) {
                lv_obj_align(s_speed_dig[i], LV_ALIGN_CENTER, x0 + i * SPEED_DIGIT_W, SPEED_Y);
                lv_obj_remove_flag(s_speed_dig[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_speed_dig[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        s_last_ndig = ndig;
    }
    for (int i = ndig - 1, x = v; i >= 0; i--, x /= 10) {
        const lv_image_dsc_t *src = &speed_digits[x % 10];
        if (lv_image_get_src(s_speed_dig[i]) != src) {
            lv_image_set_src(s_speed_dig[i], src);
        }
    }
}

static void ui_update(lv_timer_t *t)
{
    vehicle_state_t s;
    vehicle_state_get(&s);
    apply_rpm_scale(s.rpm_max, s.rpm_redline);

    if (s.connected != s_last_connected) {
        lv_label_set_text(s_kmh, s.connected ? "km/h" : "NOT CONNECTED");
        lv_obj_set_style_text_color(s_kmh, s.connected ? COL_DIM : COL_RED, 0);
        s_last_connected = s.connected;
    }

    /* Ease the needle so 5 Hz OBD data will still look smooth */
    s_rpm_disp += ((float)s.rpm - s_rpm_disp) * 0.40f;
    int rpm = (int)s_rpm_disp;
    if (rpm != lv_arc_get_value(s_arc)) {
        lv_arc_set_value(s_arc, rpm); /* only invalidates the changed slice */
    }
    /* Style changes redraw the whole arc: only touch it when the colour band changes */
    int band = rpm_band(rpm);
    if (band != s_last_band) {
        static const uint32_t band_col[] = {0x22D3EE, 0xFFB020, 0xFF2D2D};
        lv_obj_set_style_arc_color(s_arc, lv_color_hex(band_col[band]), LV_PART_INDICATOR);
        s_last_band = band;
    }

    /* Red border ring flashes while the arc is in the red zone */
    int flash_on = band == 2 && ((lv_tick_get() / FLASH_HALF_PERIOD_MS) & 1);
    if (flash_on != s_last_flash) {
        ring_set(flash_on);
        s_last_flash = flash_on;
    }

    if (s.fuel_pct != s_last_fuel) {
        bool was_low = s_last_fuel >= 0 && s_last_fuel <= FUEL_LOW_PCT;
        bool low = s.fuel_pct <= FUEL_LOW_PCT;
        lv_bar_set_value(s_fuel_bar, s.fuel_pct, LV_ANIM_OFF);
        if (low != was_low || s_last_fuel < 0) {
            lv_obj_set_style_bg_color(s_fuel_bar, low ? COL_RED : COL_TEXT, LV_PART_INDICATOR);
        }
        s_last_fuel = s.fuel_pct;
    }

    if (s.speed_kmh != s_last_speed) {
        set_speed(s.speed_kmh);
        s_last_speed = s.speed_kmh;
    }

    if (s.gear != s_last_gear) {
        if (s.gear < 0) lv_label_set_text(s_gear, "R");
        else if (s.gear == 0) lv_label_set_text(s_gear, "N");
        else lv_label_set_text_fmt(s_gear, "%d", s.gear);
        s_last_gear = s.gear;
    }

    /* Shift light: gear box flashes red near the limiter */
    bool shift = s.rpm >= s_rpm_shift && s.gear > 0;
    bool flash = shift && ((s_blink++ / 3) & 1);
    int gear_style = (shift ? 1 : 0) | (flash ? 2 : 0);
    if (gear_style != s_last_gear_style) {
        lv_obj_set_style_bg_color(s_gear_box, flash ? COL_RED : lv_color_hex(0x111111), 0);
        lv_obj_set_style_border_color(s_gear_box, shift ? COL_RED : COL_DIM, 0);
        s_last_gear_style = gear_style;
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);
    return l;
}

void gauge_ui_show_splash(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *logo = lv_image_create(scr);
    lv_image_set_src(logo, &logo_big);
    lv_obj_set_style_image_recolor(logo, COL_TEXT, 0);
    lv_obj_set_style_image_recolor_opa(logo, LV_OPA_COVER, 0);
    lv_obj_center(logo);
    lv_obj_fade_in(logo, 700, 0);
}

void gauge_ui_create(void)
{
    lv_obj_clean(lv_screen_active()); /* remove the splash */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* --- RPM bar: 270° arc, opening at the bottom --- */
    s_arc = lv_arc_create(scr);
    lv_obj_set_size(s_arc, 468, 468);
    lv_obj_center(s_arc);
    lv_arc_set_rotation(s_arc, 135);
    lv_arc_set_bg_angles(s_arc, 0, 270);
    lv_arc_set_value(s_arc, 0);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 24, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 24, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_arc, false, LV_PART_INDICATOR);

    /* --- Tick marks + 0..8 labels inside the arc --- */
    lv_obj_t *scale = s_scale = lv_scale_create(scr);
    lv_obj_set_size(scale, 412, 412);
    lv_obj_center(scale);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_angle_range(scale, 270);
    lv_scale_set_rotation(scale, 135);
    lv_scale_set_major_tick_every(scale, 4);  /* every 1000 rpm */
    lv_scale_set_label_show(scale, true);
    lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);
    lv_obj_set_style_length(scale, 20, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 4, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(scale, COL_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(scale, &lv_font_montserrat_28, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(scale, COL_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_length(scale, 10, LV_PART_ITEMS);
    lv_obj_set_style_line_width(scale, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale, COL_DIM, LV_PART_ITEMS);

    static lv_style_t red_major, red_minor;
    lv_style_init(&red_major);
    lv_style_set_line_color(&red_major, COL_RED);
    lv_style_set_text_color(&red_major, COL_RED);
    lv_style_set_line_width(&red_major, 4);
    lv_style_init(&red_minor);
    lv_style_set_line_color(&red_minor, COL_RED);
    lv_style_set_line_width(&red_minor, 2);
    lv_scale_section_t *red = s_red_section = lv_scale_add_section(scale);
    lv_scale_set_section_style_indicator(scale, red, &red_major);
    lv_scale_set_section_style_items(scale, red, &red_minor);
    apply_rpm_scale(0, 0); /* default 8000 rpm until the data source says otherwise; sets arc/scale range, ticks, labels */

    /* --- Center: speed --- */
    lv_obj_t *logo = lv_image_create(scr);
    lv_image_set_src(logo, &logo_small);
    lv_obj_set_style_image_recolor(logo, COL_DIM, 0); /* A8 image: recolor sets its colour */
    lv_obj_set_style_image_recolor_opa(logo, LV_OPA_COVER, 0);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -112);

    for (int i = 0; i < 3; i++) {
        s_speed_dig[i] = lv_image_create(scr);
        lv_obj_set_style_image_recolor(s_speed_dig[i], COL_TEXT, 0); /* A8 image: recolor = digit colour */
        lv_obj_set_style_image_recolor_opa(s_speed_dig[i], LV_OPA_COVER, 0);
        lv_image_set_src(s_speed_dig[i], &speed_digits[0]);
    }
    set_speed(0);

    s_kmh = make_label(scr, &lv_font_montserrat_20, COL_RED, "NOT CONNECTED");
    lv_obj_align(s_kmh, LV_ALIGN_CENTER, 0, 32);

    /* --- Fuel gauge: E [bar] F, between km/h and the gear box --- */
    s_fuel_bar = lv_bar_create(scr);
    lv_obj_set_size(s_fuel_bar, 150, 12);
    lv_obj_align(s_fuel_bar, LV_ALIGN_CENTER, 0, 82);
    lv_bar_set_range(s_fuel_bar, 0, 100);
    lv_obj_set_style_bg_color(s_fuel_bar, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_fuel_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_fuel_bar, COL_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_fuel_bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(s_fuel_bar, 6, LV_PART_INDICATOR);

    /* quarter marks over the bar */
    for (int i = 1; i < 4; i++) {
        lv_obj_t *tick = lv_obj_create(scr);
        lv_obj_remove_style_all(tick);
        lv_obj_set_size(tick, 2, 18);
        lv_obj_set_style_bg_color(tick, COL_BG, 0);
        lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
        lv_obj_align(tick, LV_ALIGN_CENTER, -75 + i * 150 / 4, 82);
    }

    lv_obj_t *fe = make_label(scr, &lv_font_montserrat_20, COL_RED, "E");
    lv_obj_align(fe, LV_ALIGN_CENTER, -92, 82);
    lv_obj_t *ff = make_label(scr, &lv_font_montserrat_20, COL_DIM, "F");
    lv_obj_align(ff, LV_ALIGN_CENTER, 92, 82);

    /* --- Gear indicator in the arc opening --- */
    s_gear_box = lv_obj_create(scr);
    lv_obj_set_size(s_gear_box, 92, 92);
    lv_obj_align(s_gear_box, LV_ALIGN_CENTER, 0, 166);
    lv_obj_set_style_radius(s_gear_box, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_gear_box, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(s_gear_box, 3, 0);
    lv_obj_set_style_border_color(s_gear_box, COL_DIM, 0);
    lv_obj_set_style_pad_all(s_gear_box, 0, 0);
    lv_obj_remove_flag(s_gear_box, LV_OBJ_FLAG_SCROLLABLE);

    s_gear = make_label(s_gear_box, &lv_font_montserrat_48, COL_TEXT, "N");
    lv_obj_center(s_gear);

    /* --- Red-zone flash ring, on top of everything (drawn only when flashing) --- */
    s_ring = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ring);
    lv_obj_set_size(s_ring, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_ring);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ring, ring_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    ring_build_tiles();

    lv_timer_create(ui_update, UI_PERIOD_MS, NULL);
}
