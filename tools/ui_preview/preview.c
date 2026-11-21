/* Compile the actual UI with hardware-only functions stubbed out. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../../main/ui.c"

static app_settings_t settings = {100, 100, 100, 80};
static imu_game_snapshot_t game = {.imu_ok = true, .motor_enabled = {true, true, false}};
static imu_motion_snapshot_t motion = {.imu_ok = true, .motor_enabled = {true, true, false}};
static pattern_snapshot_t pattern = {.motor_enabled = {false, true, false}};
void app_settings_init(void) {}
void app_settings_get(app_settings_t *s) {*s = settings;}
void app_settings_set_ball_sensitivity(uint16_t v) {settings.ball_sensitivity = v;}
void app_settings_set_ball_limit(uint8_t v) {settings.ball_limit = v;}
void app_settings_set_motion_sensitivity(uint16_t v) {settings.motion_sensitivity = v;}
void app_settings_set_brightness(uint8_t v) {settings.brightness = v;}
void app_settings_save(void) {}
int bsp_display_brightness_set(int v) {(void)v; return 0;}
void imu_game_init(void) {}
void imu_game_set_running(bool v) {game.running = v;}
void imu_game_set_motor_enabled(int m, bool e) {game.motor_enabled[m] = e;}
void imu_game_request_reset(void) {game.ball_x = game.ball_y = 0;}
void imu_game_get_snapshot(imu_game_snapshot_t *s) {*s = game;}
void imu_motion_set_running(bool v) {motion.running = v;}
void imu_motion_set_motor_enabled(int m, bool e) {motion.motor_enabled[m] = e;}
void imu_motion_get_snapshot(imu_motion_snapshot_t *s) {*s = motion;}
void pattern_control_init(void) {}
void pattern_control_select(pattern_id_t v) {pattern.selected = v;}
void pattern_control_set_running(bool v) {pattern.running = v;}
void pattern_control_set_motor_enabled(motor_id_t m, bool e) {pattern.motor_enabled[m] = e;}
void pattern_control_get_snapshot(pattern_snapshot_t *s) {*s = pattern;}
void power_status_get(power_status_snapshot_t *s) {*s = (power_status_snapshot_t){true, true, true, 85};}
esp_err_t power_status_init(void) {return 0;}
void ble_client_init(void) {}
bool ble_client_start_scan(void) {return true;}
bool ble_client_connect_index(size_t i) {(void)i; return true;}
size_t ble_client_get_results(ble_scan_result_t *out, size_t cap, uint32_t *gen) {
    *gen = 1;
    if (!cap) return 0;
    out[0] = (ble_scan_result_t){"SOSEXY", "44:BD:8D:60:D0:7A", -42};
    return 1;
}
ble_app_state_t ble_client_get_state(char *peer, size_t cap) {
    if (cap) snprintf(peer, cap, "SOSEXY");
    return BLE_APP_READY;
}
bool ble_client_is_ready(void) {return true;}
void ble_client_request_motor(motor_id_t m, uint8_t v) {(void)m; (void)v;}
void ble_client_request_motor_from(ble_control_source_t s, motor_id_t m, uint8_t v) {(void)s; (void)m; (void)v;}
void ble_client_clear_source(ble_control_source_t s) {(void)s;}
void ble_client_stop_all(void) {}

static unsigned char framebuffer[480 * 480 * 4];
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *px) {
    int w = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y) {
        memcpy(framebuffer + (y * 480 + area->x1) * 4,
               px + (y - area->y1) * w * 4, w * 4);
    }
    lv_display_flush_ready(display);
}

static int layout_warnings;
static void home_metrics(lv_obj_t *page, const char *name) {
    lv_obj_t *frame = lv_obj_get_child(page, 0);
    lv_area_t f;
    lv_obj_get_coords(frame, &f);
    printf("%s frame [%d,%d,%d,%d]\n", name, f.x1, f.y1, f.x2, f.y2);
    for (uint32_t i = 0; i < 4; ++i) {
        lv_area_t a;
        lv_obj_get_coords(lv_obj_get_child(frame, i), &a);
        printf("  card %u [%d,%d,%d,%d] size %dx%d\n", i, a.x1, a.y1, a.x2, a.y2,
               a.x2 - a.x1 + 1, a.y2 - a.y1 + 1);
    }
}

static lv_obj_t *find_slider(lv_obj_t *obj) {
    if (lv_obj_check_type(obj, &lv_slider_class)) return obj;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
        lv_obj_t *found = find_slider(lv_obj_get_child(obj, i));
        if (found) return found;
    }
    return NULL;
}

static void inspect(lv_obj_t *obj, const char *page) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_area_t a, p;
        lv_obj_get_coords(obj, &a);
        lv_obj_get_coords(lv_obj_get_parent(obj), &p);
        if (a.x2 >= 0 && a.x1 < 480 && a.y2 >= 0 && a.y1 < 480 &&
            (a.x1 < p.x1 || a.x2 > p.x2 || a.y1 < p.y1 || a.y2 > p.y2)) {
            printf("LABEL OUTSIDE PARENT [%s] %s [%d,%d,%d,%d] parent [%d,%d,%d,%d]\n",
                   page, lv_label_get_text(obj), a.x1, a.y1, a.x2, a.y2, p.x1, p.y1, p.x2, p.y2);
            ++layout_warnings;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) inspect(lv_obj_get_child(obj, i), page);
}

static void capture(const char *name, lv_obj_t *view) {
    show_view(view);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    char filename[128];
    snprintf(filename, sizeof filename, "screenshots/%s.ppm", name);
    FILE *f = fopen(filename, "wb");
    if (!f) {perror(filename); exit(1);}
    fprintf(f, "P6\n480 480\n255\n");
    for (int i = 0; i < 480 * 480; ++i) {
        fputc(framebuffer[i * 4 + 2], f);
        fputc(framebuffer[i * 4 + 1], f);
        fputc(framebuffer[i * 4], f);
    }
    fclose(f);
    inspect(view, name);
    printf("Captured %s\n", filename);
}

int main(void) {
    mkdir("screenshots", 0777);
    lv_init();
    lv_display_t *display = lv_display_create(480, 480);
    static uint8_t render[480 * 480 * 4];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, render, NULL, sizeof render, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    app_ui_create();
    ui_timer_cb(NULL);
    lv_tileview_set_tile_by_index(s_home_tileview, 0, 0, LV_ANIM_OFF);
    capture("home", s_home_view);
    home_metrics(lv_obj_get_child(s_home_tileview, 0), "home");
    lv_tileview_set_tile_by_index(s_home_tileview, 1, 0, LV_ANIM_OFF);
    capture("home_more", s_home_view);
    home_metrics(lv_obj_get_child(s_home_tileview, 1), "home_more");
    lv_tileview_set_tile_by_index(s_control_tileview, 0, 0, LV_ANIM_OFF);
    capture("control", s_control_view);
    lv_obj_t *slider = find_slider(s_control_view);
    if (slider) {
        lv_slider_set_value(slider, 50, LV_ANIM_OFF);
        lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
        capture("control_50", s_control_view);
        lv_slider_set_value(slider, 100, LV_ANIM_OFF);
        lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
        capture("control_100", s_control_view);
    }
    capture("bluetooth", s_ble_view);
    capture("ball", s_game_view);
    capture("motion", s_motion_view);
    capture("patterns", s_pattern_view);
    capture("settings", s_settings_view);
    printf("Layout warnings: %d\n", layout_warnings);
    return layout_warnings ? 2 : 0;
}
