#include "ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_settings.h"
#include "ble_client.h"
#include "esp_err.h"
#include "bsp/display.h"
#include "imu_game.h"
#include "lvgl.h"
#include "power_status.h"
#include "pattern_control.h"

#define COLOR_BG       0x12000F
#define COLOR_CARD     0x2A0828
#define COLOR_CARD_2   0x3D0B38
#define COLOR_MAGENTA  0xE91E8C
#define COLOR_PURPLE   0x9C27FF
#define COLOR_PINK     0xFF64B4
#define COLOR_TEXT     0xFFF2FA
#define COLOR_MUTED    0xC9A7C1
#define COLOR_GREEN    0x58E6A9
#define COLOR_RED      0xFF426D
#define STATUS_COUNT   7

typedef enum {
    HOME_CONTROL,
    HOME_BLE,
    HOME_GAME,
    HOME_MOTION,
    HOME_SETTINGS,
    HOME_PATTERN,
    HOME_PENDING,
} home_action_t;

typedef enum {
    SETTING_BALL_SENSITIVITY,
    SETTING_BALL_LIMIT,
    SETTING_MOTION_SENSITIVITY,
    SETTING_BRIGHTNESS,
    SETTING_COUNT,
} setting_id_t;

static lv_obj_t *s_home_view;
static lv_obj_t *s_home_tileview;
static lv_obj_t *s_ble_view;
static lv_obj_t *s_control_view;
static lv_obj_t *s_game_view;
static lv_obj_t *s_motion_view;
static lv_obj_t *s_settings_view;
static lv_obj_t *s_pattern_view;
static lv_obj_t *s_control_tileview;
static lv_obj_t *s_status_labels[STATUS_COUNT];
static lv_obj_t *s_device_list;
static lv_obj_t *s_slider_labels[MOTOR_COUNT];
static lv_obj_t *s_game_ball;
static lv_obj_t *s_game_value;
static lv_obj_t *s_game_hint;
static lv_obj_t *s_game_start_label;
static lv_obj_t *s_game_switches[MOTOR_COUNT];
static lv_obj_t *s_motion_value;
static lv_obj_t *s_motion_accel;
static lv_obj_t *s_motion_state;
static lv_obj_t *s_motion_start_label;
static lv_obj_t *s_motion_bar;
static lv_obj_t *s_motion_switches[MOTOR_COUNT];
static lv_obj_t *s_setting_values[SETTING_COUNT];
static lv_obj_t *s_battery_label;
static lv_obj_t *s_battery_bar;
static lv_obj_t *s_pattern_buttons[PATTERN_COUNT];
static lv_obj_t *s_pattern_switches[MOTOR_COUNT];
static lv_obj_t *s_pattern_value;
static lv_obj_t *s_pattern_start_label;
static uint32_t s_seen_scan_generation = UINT32_MAX;
static lv_coord_t s_back_start_y;
static bool s_back_drag_active;

static const lv_font_t *cjk_font(void)
{
    return &lv_font_source_han_sans_sc_16_cjk;
}

static void set_bg(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static void show_view(lv_obj_t *view)
{
    lv_obj_add_flag(s_home_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ble_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_control_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_game_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_motion_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_settings_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pattern_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(view);
}

static void stop_event(lv_event_t *event)
{
    (void)event;
    imu_game_set_running(false);
    imu_motion_set_running(false);
    pattern_control_set_running(false);
    ble_client_stop_all();
}

static void scan_event(lv_event_t *event)
{
    (void)event;
    ble_client_start_scan();
}

static void connect_event(lv_event_t *event)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    ble_client_connect_index(index);
}

static void home_button_event(lv_event_t *event)
{
    home_action_t action = (home_action_t)(uintptr_t)lv_event_get_user_data(event);
    switch (action) {
    case HOME_CONTROL:
        lv_tileview_set_tile_by_index(s_control_tileview, 0, 0, LV_ANIM_OFF);
        show_view(s_control_view);
        break;
    case HOME_BLE:
        show_view(s_ble_view);
        ble_client_start_scan();
        break;
    case HOME_GAME:
        show_view(s_game_view);
        break;
    case HOME_MOTION:
        show_view(s_motion_view);
        break;
    case HOME_SETTINGS:
        show_view(s_settings_view);
        break;
    case HOME_PATTERN:
        show_view(s_pattern_view);
        break;
    case HOME_PENDING:
        break;
    default:
        break;
    }
}

static void back_swipe_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_event_get_indev(event);
    lv_point_t point;

    if (indev == NULL) {
        return;
    }
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        s_back_start_y = point.y;
        s_back_drag_active = true;
    } else if (code == LV_EVENT_RELEASED && s_back_drag_active) {
        s_back_drag_active = false;
        if (s_back_start_y - point.y >= 38) {
            int page = (int)(uintptr_t)lv_event_get_user_data(event);
            lv_tileview_set_tile_by_index(s_home_tileview, page, 0, LV_ANIM_OFF);
            show_view(s_home_view);
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        s_back_drag_active = false;
    }
}

static lv_obj_t *create_header(lv_obj_t *page, const char *title,
                               const lv_font_t *title_font, int status_index)
{
    lv_obj_t *header = lv_obj_create(page);
    lv_obj_set_size(header, 440, 58);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 10);
    set_bg(header, COLOR_CARD);
    lv_obj_set_style_radius(header, 22, 0);
    lv_obj_set_style_pad_all(header, 12, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = make_label(header, title, title_font, COLOR_TEXT);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 4, -7);

    s_status_labels[status_index] = make_label(header, "BLE STARTING",
                                               &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(s_status_labels[status_index], LV_ALIGN_LEFT_MID, 5, 15);

    lv_obj_t *stop = lv_button_create(header);
    lv_obj_set_size(stop, 86, 38);
    lv_obj_align(stop, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(stop, 19, 0);
    lv_obj_set_style_bg_color(stop, lv_color_hex(COLOR_RED), 0);
    lv_obj_add_event_cb(stop, stop_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stop_label = make_label(stop, "STOP", &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_center(stop_label);
    return header;
}

static void create_back_bar(lv_obj_t *page, int home_page)
{
    lv_obj_t *bar = lv_button_create(page);
    lv_obj_set_size(bar, 196, 38);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_radius(bar, 19, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_CARD_2), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(COLOR_MAGENTA), 0);
    lv_obj_set_style_shadow_width(bar, 14, 0);
    lv_obj_set_style_shadow_color(bar, lv_color_hex(COLOR_MAGENTA), 0);
    lv_obj_add_event_cb(bar, back_swipe_event, LV_EVENT_PRESSED,
                        (void *)(uintptr_t)home_page);
    lv_obj_add_event_cb(bar, back_swipe_event, LV_EVENT_RELEASED,
                        (void *)(uintptr_t)home_page);
    lv_obj_add_event_cb(bar, back_swipe_event, LV_EVENT_PRESS_LOST,
                        (void *)(uintptr_t)home_page);

    lv_obj_t *label = make_label(bar,
                                 home_page == 0 ? "^  SWIPE UP  -  HOME" :
                                                  "^  SWIPE UP  -  MENU",
                                 &lv_font_montserrat_12, COLOR_TEXT);
    lv_obj_center(label);
}

static lv_obj_t *create_root_view(lv_obj_t *screen)
{
    lv_obj_t *view = lv_obj_create(screen);
    lv_obj_set_size(view, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(view, 0, 0);
    set_bg(view, COLOR_BG);
    lv_obj_set_style_pad_all(view, 0, 0);
    lv_obj_remove_flag(view, LV_OBJ_FLAG_SCROLLABLE);
    return view;
}

static lv_obj_t *create_home_button(lv_obj_t *parent, int x, int y,
                                    const char *title, const char *subtitle,
                                    const lv_font_t *title_font, home_action_t action)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 174, 140);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 28, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_CARD_2), 0);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(button, 3, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(COLOR_MAGENTA), 0);
    lv_obj_set_style_shadow_width(button, 18, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(COLOR_MAGENTA), 0);
    lv_obj_add_event_cb(button, home_button_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)action);

    lv_obj_t *title_label = make_label(button, title, title_font, COLOR_TEXT);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, -12);
    lv_obj_t *subtitle_label = make_label(button, subtitle, &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(subtitle_label, LV_ALIGN_CENTER, 0, 18);

    return button;
}

static lv_obj_t *create_home_frame(lv_obj_t *page, const char *title,
                                   const lv_font_t *title_font)
{
    set_bg(page, COLOR_BG);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_t *frame = lv_obj_create(page);
    lv_obj_set_size(frame, 440, 440);
    lv_obj_center(frame);
    set_bg(frame, COLOR_CARD);
    lv_obj_set_style_radius(frame, 36, 0);
    lv_obj_set_style_border_width(frame, 3, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(COLOR_MAGENTA), 0);
    lv_obj_set_style_shadow_width(frame, 24, 0);
    lv_obj_set_style_shadow_color(frame, lv_color_hex(COLOR_PURPLE), 0);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brand = make_label(frame, title, title_font, COLOR_PINK);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 13);
    return frame;
}

static void create_home_view(lv_obj_t *screen)
{
    s_home_view = create_root_view(screen);
    s_home_tileview = lv_tileview_create(s_home_view);
    lv_obj_set_size(s_home_tileview, lv_pct(100), lv_pct(100));
    set_bg(s_home_tileview, COLOR_BG);
    lv_obj_set_style_pad_all(s_home_tileview, 0, 0);
    lv_obj_remove_flag(s_home_tileview, LV_OBJ_FLAG_SCROLL_ELASTIC);

    lv_obj_t *main_page = lv_tileview_add_tile(s_home_tileview, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *more_page = lv_tileview_add_tile(s_home_tileview, 1, 0, LV_DIR_LEFT);

    lv_obj_t *frame = create_home_frame(main_page, "SOSEXY REMOTE",
                                        &lv_font_montserrat_22);

    create_home_button(frame, 30, 70, "控制", "CONTROL", cjk_font(), HOME_CONTROL);
    create_home_button(frame, 236, 70, "BLE", "BLUETOOTH", &lv_font_montserrat_22, HOME_BLE);
    create_home_button(frame, 30, 232, "小球", "BALANCE", cjk_font(), HOME_GAME);
    create_home_button(frame, 236, 232, "体感", "MOTION", cjk_font(), HOME_MOTION);

    lv_obj_t *hint = make_label(frame, "Tap a card  |  SWIPE LEFT FOR MORE",
                                &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);

    lv_obj_t *more_frame = create_home_frame(more_page, "MORE FEATURES",
                                             &lv_font_montserrat_22);
    create_home_button(more_frame, 30, 70, "SETTINGS", "DEVICE CONFIG",
                       &lv_font_montserrat_22, HOME_SETTINGS);
    create_home_button(more_frame, 236, 70, "PATTERNS", "VIBRATION",
                       &lv_font_montserrat_22, HOME_PATTERN);
    create_home_button(more_frame, 30, 232, "PENDING", "COMING SOON",
                       &lv_font_montserrat_22, HOME_PENDING);
    create_home_button(more_frame, 236, 232, "PENDING", "COMING SOON",
                       &lv_font_montserrat_22, HOME_PENDING);
    lv_obj_t *more_hint = make_label(more_frame, "SWIPE RIGHT FOR HOME",
                                     &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(more_hint, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void slider_event(lv_event_t *event)
{
    motor_id_t motor = (motor_id_t)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *slider = lv_event_get_target_obj(event);
    int32_t value = lv_slider_get_value(slider);
    char text[8];
    snprintf(text, sizeof(text), "%ld%%", (long)value);
    lv_label_set_text(s_slider_labels[motor], text);

    ble_client_request_motor(motor, (uint8_t)value);
}

static void create_slider_page(lv_obj_t *page, motor_id_t motor,
                               const char *title, const lv_font_t *title_font,
                               int status_index)
{
    set_bg(page, COLOR_BG);
    lv_obj_set_style_pad_all(page, 0, 0);
    create_header(page, title, title_font, status_index);

    lv_obj_t *card = lv_obj_create(page);
    lv_obj_set_size(card, 330, 325);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 78);
    set_bg(card, COLOR_CARD);
    lv_obj_set_style_radius(card, 34, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_MAGENTA), 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *caption = make_label(card, "INTENSITY  0 - 100",
                                   &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_size(slider, 108, 238);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_LEFT, 40, -18);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_CARD_2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_MAGENTA), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(slider, lv_color_hex(COLOR_PURPLE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(slider, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 50, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_PINK), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 10, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 22, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(slider, lv_color_hex(COLOR_MAGENTA), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, slider_event, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)motor);

    s_slider_labels[motor] = make_label(card, "0%", &lv_font_montserrat_40, COLOR_TEXT);
    lv_obj_align(s_slider_labels[motor], LV_ALIGN_RIGHT_MID, -28, 10);

    lv_obj_t *swipe = make_label(card, "<  SWIPE  >",
                                 &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(swipe, LV_ALIGN_BOTTOM_RIGHT, -24, -30);

    create_back_bar(page, 0);
}

static void rebuild_device_list(void)
{
    ble_scan_result_t results[BLE_MAX_SCAN_RESULTS];
    uint32_t generation;
    size_t count = ble_client_get_results(results, BLE_MAX_SCAN_RESULTS, &generation);
    if (generation == s_seen_scan_generation) {
        return;
    }
    s_seen_scan_generation = generation;
    lv_obj_clean(s_device_list);

    if (count == 0) {
        lv_obj_t *empty = make_label(s_device_list, "Scanning for BLE devices...",
                                     &lv_font_montserrat_16, COLOR_MUTED);
        lv_obj_center(empty);
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *row = lv_button_create(s_device_list);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 64);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(strcmp(results[i].name, "SOSEXY") == 0 ? COLOR_MAGENTA : COLOR_CARD_2), 0);
        lv_obj_add_event_cb(row, connect_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        char line[80];
        snprintf(line, sizeof(line), "%s\n%s   %d dBm",
                 results[i].name[0] ? results[i].name : "Unnamed",
                 results[i].address, results[i].rssi);
        lv_obj_t *label = make_label(row, line, &lv_font_montserrat_14, COLOR_TEXT);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 2, 0);
    }
}

static void create_ble_view(lv_obj_t *screen)
{
    s_ble_view = create_root_view(screen);
    create_header(s_ble_view, "BLUETOOTH", &lv_font_montserrat_22, 0);

    lv_obj_t *scan = lv_button_create(s_ble_view);
    lv_obj_set_size(scan, 160, 48);
    lv_obj_align(scan, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_bg_color(scan, lv_color_hex(COLOR_PURPLE), 0);
    lv_obj_set_style_radius(scan, 24, 0);
    lv_obj_add_event_cb(scan, scan_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_label = make_label(scan, "SCAN AGAIN", &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_center(scan_label);

    s_device_list = lv_obj_create(s_ble_view);
    lv_obj_set_size(s_device_list, 420, 286);
    lv_obj_align(s_device_list, LV_ALIGN_TOP_MID, 0, 134);
    set_bg(s_device_list, COLOR_CARD);
    lv_obj_set_style_radius(s_device_list, 28, 0);
    lv_obj_set_flex_flow(s_device_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_device_list, 12, 0);
    lv_obj_set_style_pad_row(s_device_list, 9, 0);
    create_back_bar(s_ble_view, 0);
}

static void create_control_view(lv_obj_t *screen)
{
    s_control_view = create_root_view(screen);
    s_control_tileview = lv_tileview_create(s_control_view);
    lv_obj_set_size(s_control_tileview, lv_pct(100), lv_pct(100));
    set_bg(s_control_tileview, COLOR_BG);
    lv_obj_set_style_pad_all(s_control_tileview, 0, 0);
    lv_obj_remove_flag(s_control_tileview, LV_OBJ_FLAG_SCROLL_ELASTIC);

    lv_obj_t *suction = lv_tileview_add_tile(s_control_tileview, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *vibration = lv_tileview_add_tile(s_control_tileview, 1, 0,
                                               LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *electric = lv_tileview_add_tile(s_control_tileview, 2, 0, LV_DIR_LEFT);

    create_slider_page(suction, MOTOR_SUCTION, "SUCTION", &lv_font_montserrat_22, 1);
    create_slider_page(vibration, MOTOR_VIBRATION, "震动", cjk_font(), 2);
    create_slider_page(electric, MOTOR_ELECTRIC, "电流", cjk_font(), 3);
}

static void game_start_event(lv_event_t *event)
{
    (void)event;
    imu_game_snapshot_t snapshot;
    imu_game_get_snapshot(&snapshot);
    imu_game_set_running(!snapshot.running);
    if (!snapshot.running) {
        for (int i = 0; i < MOTOR_COUNT; ++i) {
            bool enabled = i != MOTOR_ELECTRIC;
            if (enabled) {
                lv_obj_add_state(s_game_switches[i], LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(s_game_switches[i], LV_STATE_CHECKED);
            }
        }
    }
}

static void game_reset_event(lv_event_t *event)
{
    (void)event;
    imu_game_request_reset();
}

static void game_motor_event(lv_event_t *event)
{
    int motor = (int)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *sw = lv_event_get_target_obj(event);
    imu_game_set_motor_enabled(motor, lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static lv_obj_t *make_small_button(lv_obj_t *parent, const char *text, int width,
                                   lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, 40);
    lv_obj_set_style_radius(button, 20, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_PURPLE), 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = make_label(button, text, &lv_font_montserrat_14, 0xFFFFFF);
    lv_obj_center(label);
    return button;
}

static void create_game_view(lv_obj_t *screen)
{
    s_game_view = create_root_view(screen);
    create_header(s_game_view, "BALANCE BALL", &lv_font_montserrat_22, 4);

    lv_obj_t *arena = lv_obj_create(s_game_view);
    lv_obj_set_size(arena, 300, 215);
    lv_obj_align(arena, LV_ALIGN_TOP_MID, 0, 76);
    set_bg(arena, COLOR_CARD);
    lv_obj_set_style_radius(arena, 38, 0);
    lv_obj_set_style_border_width(arena, 3, 0);
    lv_obj_set_style_border_color(arena, lv_color_hex(COLOR_MAGENTA), 0);
    lv_obj_remove_flag(arena, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ring = lv_obj_create(arena);
    lv_obj_set_size(ring, 76, 76);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(COLOR_MUTED), 0);

    s_game_ball = lv_obj_create(arena);
    lv_obj_set_size(s_game_ball, 44, 44);
    lv_obj_center(s_game_ball);
    lv_obj_set_style_radius(s_game_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_game_ball, lv_color_hex(COLOR_PINK), 0);
    lv_obj_set_style_border_width(s_game_ball, 0, 0);
    lv_obj_set_style_shadow_width(s_game_ball, 24, 0);
    lv_obj_set_style_shadow_color(s_game_ball, lv_color_hex(COLOR_MAGENTA), 0);

    s_game_value = make_label(arena, "0", &lv_font_montserrat_40, COLOR_TEXT);
    lv_obj_align(s_game_value, LV_ALIGN_TOP_RIGHT, -16, 8);
    s_game_hint = make_label(arena, "Press BOOT to reset center",
                             &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(s_game_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t *controls = lv_obj_create(s_game_view);
    lv_obj_set_size(controls, 440, 124);
    lv_obj_align(controls, LV_ALIGN_TOP_MID, 0, 302);
    set_bg(controls, COLOR_CARD);
    lv_obj_set_style_radius(controls, 28, 0);
    lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    const char *names[MOTOR_COUNT] = {"S", "V", "E"};
    for (int i = 0; i < MOTOR_COUNT; ++i) {
        int x = -128 + i * 82;
        lv_obj_t *label = make_label(controls, names[i], &lv_font_montserrat_14, COLOR_TEXT);
        lv_obj_align(label, LV_ALIGN_TOP_MID, x, -2);
        s_game_switches[i] = lv_switch_create(controls);
        lv_obj_set_size(s_game_switches[i], 54, 28);
        lv_obj_align(s_game_switches[i], LV_ALIGN_TOP_MID, x, 20);
        lv_obj_set_style_bg_color(s_game_switches[i], lv_color_hex(COLOR_MAGENTA),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (i != MOTOR_ELECTRIC) {
            lv_obj_add_state(s_game_switches[i], LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(s_game_switches[i], game_motor_event,
                            LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
    }

    lv_obj_t *start = make_small_button(controls, "START", 112, game_start_event);
    lv_obj_align(start, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
    s_game_start_label = lv_obj_get_child(start, 0);
    lv_obj_t *reset = make_small_button(controls, "CENTER", 104, game_reset_event);
    lv_obj_align(reset, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    create_back_bar(s_game_view, 0);
}

static void motion_start_event(lv_event_t *event)
{
    (void)event;
    imu_motion_snapshot_t snapshot;
    imu_motion_get_snapshot(&snapshot);
    imu_motion_set_running(!snapshot.running);
}

static void motion_motor_event(lv_event_t *event)
{
    int motor = (int)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *sw = lv_event_get_target_obj(event);
    imu_motion_set_motor_enabled(motor, lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void create_motion_view(lv_obj_t *screen)
{
    s_motion_view = create_root_view(screen);
    create_header(s_motion_view, "MOTION CONTROL", &lv_font_montserrat_22, 5);

    lv_obj_t *card = lv_obj_create(s_motion_view);
    lv_obj_set_size(card, 400, 340);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 80);
    set_bg(card, COLOR_CARD);
    lv_obj_set_style_radius(card, 34, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_MAGENTA), 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *caption = make_label(card, "MOVE FASTER  =  STRONGER OUTPUT",
                                   &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, 10);

    s_motion_value = make_label(card, "0%", &lv_font_montserrat_40, COLOR_TEXT);
    lv_obj_align(s_motion_value, LV_ALIGN_TOP_MID, 0, 42);

    s_motion_accel = make_label(card, "LINEAR ACCEL  0.00 m/s2",
                                &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_align(s_motion_accel, LV_ALIGN_TOP_MID, 0, 96);

    s_motion_bar = lv_bar_create(card);
    lv_obj_set_size(s_motion_bar, 320, 48);
    lv_obj_align(s_motion_bar, LV_ALIGN_TOP_MID, 0, 132);
    lv_bar_set_range(s_motion_bar, 0, 100);
    lv_bar_set_value(s_motion_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_motion_bar, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_motion_bar, lv_color_hex(COLOR_CARD_2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_motion_bar, lv_color_hex(COLOR_MAGENTA), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(s_motion_bar, lv_color_hex(COLOR_PURPLE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(s_motion_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_motion_bar, 24, LV_PART_INDICATOR);

    s_motion_state = make_label(card, "STOPPED", &lv_font_montserrat_16, COLOR_MUTED);
    lv_obj_align(s_motion_state, LV_ALIGN_TOP_MID, 0, 188);

    const char *motion_names[MOTOR_COUNT] = {"S", "V", "E"};
    for (int i = 0; i < MOTOR_COUNT; ++i) {
        int x = -108 + i * 108;
        lv_obj_t *label = make_label(card, motion_names[i],
                                     &lv_font_montserrat_14, COLOR_TEXT);
        lv_obj_align(label, LV_ALIGN_TOP_MID, x, 216);
        s_motion_switches[i] = lv_switch_create(card);
        lv_obj_set_size(s_motion_switches[i], 56, 28);
        lv_obj_align(s_motion_switches[i], LV_ALIGN_TOP_MID, x, 236);
        lv_obj_set_style_bg_color(s_motion_switches[i], lv_color_hex(COLOR_MAGENTA),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (i != MOTOR_ELECTRIC) {
            lv_obj_add_state(s_motion_switches[i], LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(s_motion_switches[i], motion_motor_event,
                            LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
    }

    lv_obj_t *start = make_small_button(card, "START", 150, motion_start_event);
    lv_obj_set_height(start, 48);
    lv_obj_align(start, LV_ALIGN_BOTTOM_MID, 0, -10);
    s_motion_start_label = lv_obj_get_child(start, 0);

    create_back_bar(s_motion_view, 0);
}

static void pattern_select_event(lv_event_t *event)
{
    pattern_id_t pattern = (pattern_id_t)(uintptr_t)lv_event_get_user_data(event);
    pattern_control_select(pattern);
}

static void pattern_start_event(lv_event_t *event)
{
    (void)event;
    pattern_snapshot_t snapshot;
    pattern_control_get_snapshot(&snapshot);
    pattern_control_set_running(!snapshot.running);
    if (!snapshot.running) {
        lv_obj_remove_state(s_pattern_switches[MOTOR_ELECTRIC], LV_STATE_CHECKED);
    }
}

static void pattern_motor_event(lv_event_t *event)
{
    motor_id_t motor = (motor_id_t)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *sw = lv_event_get_target_obj(event);
    pattern_control_set_motor_enabled(motor,
        lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void create_pattern_view(lv_obj_t *screen)
{
    s_pattern_view = create_root_view(screen);
    create_header(s_pattern_view, "VIBRATION PATTERNS",
                  &lv_font_montserrat_22, 6);

    lv_obj_t *card = lv_obj_create(s_pattern_view);
    lv_obj_set_size(card, 420, 340);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 80);
    set_bg(card, COLOR_CARD);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_MAGENTA), 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    const char *titles[PATTERN_COUNT] = {
        "1  RAMP  0 > 100 > 0",
        "2  RANDOM  20 / 40 / 60 / 80 / 100",
        "3  PULSE  100%  1s ON / 1s OFF",
    };
    for (int i = 0; i < PATTERN_COUNT; ++i) {
        lv_obj_t *button = lv_button_create(card);
        s_pattern_buttons[i] = button;
        lv_obj_set_size(button, 376, 49);
        lv_obj_set_pos(button, 8, 9 + i * 57);
        lv_obj_set_style_radius(button, 16, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_CARD_2), 0);
        lv_obj_add_event_cb(button, pattern_select_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        lv_obj_t *label = make_label(button, titles[i],
                                     &lv_font_montserrat_14, COLOR_TEXT);
        lv_obj_center(label);
    }

    s_pattern_value = make_label(card, "0%", &lv_font_montserrat_22, COLOR_PINK);
    lv_obj_align(s_pattern_value, LV_ALIGN_TOP_MID, 0, 185);

    const char *motor_names[MOTOR_COUNT] = {"SUCTION", "VIBRATION", "ELECTRIC"};
    for (int i = 0; i < MOTOR_COUNT; ++i) {
        int x = -125 + i * 125;
        lv_obj_t *label = make_label(card, motor_names[i],
                                     &lv_font_montserrat_12, COLOR_TEXT);
        lv_obj_align(label, LV_ALIGN_TOP_MID, x, 213);
        s_pattern_switches[i] = lv_switch_create(card);
        lv_obj_set_size(s_pattern_switches[i], 56, 28);
        lv_obj_align(s_pattern_switches[i], LV_ALIGN_TOP_MID, x, 233);
        lv_obj_set_style_bg_color(s_pattern_switches[i],
            lv_color_hex(COLOR_MAGENTA), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (i == MOTOR_VIBRATION) {
            lv_obj_add_state(s_pattern_switches[i], LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(s_pattern_switches[i], pattern_motor_event,
                            LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
    }

    lv_obj_t *start = make_small_button(card, "START", 150, pattern_start_event);
    lv_obj_set_height(start, 46);
    lv_obj_align(start, LV_ALIGN_BOTTOM_MID, 0, -3);
    s_pattern_start_label = lv_obj_get_child(start, 0);
    create_back_bar(s_pattern_view, 1);
}

static void update_setting_label(setting_id_t id, int32_t value)
{
    if (id < 0 || id >= SETTING_COUNT || s_setting_values[id] == NULL) {
        return;
    }
    char text[12];
    snprintf(text, sizeof(text), "%ld%%", (long)value);
    lv_label_set_text(s_setting_values[id], text);
}

static void setting_slider_event(lv_event_t *event)
{
    setting_id_t id = (setting_id_t)(uintptr_t)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *slider = lv_event_get_target_obj(event);

    if (code == LV_EVENT_RELEASED) {
        app_settings_save();
        return;
    }
    if (code != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    int32_t value = lv_slider_get_value(slider);
    update_setting_label(id, value);
    switch (id) {
    case SETTING_BALL_SENSITIVITY:
        app_settings_set_ball_sensitivity((uint16_t)value);
        break;
    case SETTING_BALL_LIMIT:
        app_settings_set_ball_limit((uint8_t)value);
        break;
    case SETTING_MOTION_SENSITIVITY:
        app_settings_set_motion_sensitivity((uint16_t)value);
        break;
    case SETTING_BRIGHTNESS:
        app_settings_set_brightness((uint8_t)value);
        bsp_display_brightness_set((int)value);
        break;
    default:
        break;
    }
}

static void create_setting_slider(lv_obj_t *parent, int y, setting_id_t id,
                                  const char *title, int min, int max, int value)
{
    lv_obj_t *title_label = make_label(parent, title, &lv_font_montserrat_14,
                                       COLOR_TEXT);
    lv_obj_set_pos(title_label, 16, y + 3);

    s_setting_values[id] = make_label(parent, "", &lv_font_montserrat_14,
                                      COLOR_PINK);
    lv_obj_set_pos(s_setting_values[id], 358, y + 3);
    update_setting_label(id, value);

    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_size(slider, 386, 18);
    lv_obj_set_pos(slider, 15, y + 30);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_radius(slider, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_CARD_2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_MAGENTA), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(slider, lv_color_hex(COLOR_PURPLE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(slider, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 9, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_PINK), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, setting_slider_event, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)id);
    lv_obj_add_event_cb(slider, setting_slider_event, LV_EVENT_RELEASED,
                        (void *)(uintptr_t)id);
}

static void create_settings_view(lv_obj_t *screen)
{
    app_settings_t settings;
    app_settings_get(&settings);

    s_settings_view = create_root_view(screen);

    lv_obj_t *header = lv_obj_create(s_settings_view);
    lv_obj_set_size(header, 440, 58);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 10);
    set_bg(header, COLOR_CARD);
    lv_obj_set_style_radius(header, 22, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = make_label(header, "SETTINGS", &lv_font_montserrat_22,
                                 COLOR_TEXT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, -8);
    lv_obj_t *subtitle = make_label(header, "DEVICE CONFIG",
                                    &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(subtitle, LV_ALIGN_LEFT_MID, 12, 14);

    lv_obj_t *card = lv_obj_create(s_settings_view);
    lv_obj_set_size(card, 440, 346);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 76);
    set_bg(card, COLOR_CARD);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_MAGENTA), 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    create_setting_slider(card, 0, SETTING_BALL_SENSITIVITY,
                          "BALL SENSITIVITY", 50, 200, settings.ball_sensitivity);
    create_setting_slider(card, 61, SETTING_BALL_LIMIT,
                          "BALL OUTPUT LIMIT", 20, 100, settings.ball_limit);
    create_setting_slider(card, 122, SETTING_MOTION_SENSITIVITY,
                          "MOTION SENSITIVITY", 50, 200, settings.motion_sensitivity);
    create_setting_slider(card, 183, SETTING_BRIGHTNESS,
                          "DISPLAY BRIGHTNESS", 10, 100, settings.brightness);

    lv_obj_t *battery_title = make_label(card, "BATTERY LEVEL",
                                         &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_set_pos(battery_title, 16, 252);
    s_battery_label = make_label(card, "READING...", &lv_font_montserrat_14,
                                 COLOR_MUTED);
    lv_obj_set_pos(s_battery_label, 265, 252);

    s_battery_bar = lv_bar_create(card);
    lv_obj_set_size(s_battery_bar, 386, 24);
    lv_obj_set_pos(s_battery_bar, 15, 286);
    lv_bar_set_range(s_battery_bar, 0, 100);
    lv_bar_set_value(s_battery_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_battery_bar, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_battery_bar, lv_color_hex(COLOR_CARD_2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_battery_bar, lv_color_hex(COLOR_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_battery_bar, 12, LV_PART_INDICATOR);

    create_back_bar(s_settings_view, 1);
}

static const char *state_text(ble_app_state_t state)
{
    switch (state) {
    case BLE_APP_STARTING: return "BLE STARTING";
    case BLE_APP_IDLE: return "BLE IDLE";
    case BLE_APP_SCANNING: return "SCANNING";
    case BLE_APP_CONNECTING: return "CONNECTING";
    case BLE_APP_DISCOVERING: return "GATT SETUP";
    case BLE_APP_READY: return "CONNECTED";
    default: return "BLE ERROR";
    }
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    rebuild_device_list();

    char peer[32];
    ble_app_state_t state = ble_client_get_state(peer, sizeof(peer));
    char status[64];
    snprintf(status, sizeof(status), "%s%s%s", state_text(state),
             peer[0] ? "  " : "", peer);
    for (int i = 0; i < STATUS_COUNT; ++i) {
        if (s_status_labels[i] != NULL) {
            lv_label_set_text(s_status_labels[i], status);
            lv_obj_set_style_text_color(s_status_labels[i],
                lv_color_hex(state == BLE_APP_READY ? COLOR_GREEN : COLOR_MUTED), 0);
        }
    }

    imu_game_snapshot_t game;
    imu_game_get_snapshot(&game);
    lv_obj_align(s_game_ball, LV_ALIGN_CENTER,
                 (int)(game.ball_x * 108.0f),
                 (int)(game.ball_y * 66.0f));
    char value[8];
    snprintf(value, sizeof(value), "%u", game.intensity);
    lv_label_set_text(s_game_value, value);
    lv_label_set_text(s_game_start_label, game.running ? "STOP" : "START");
    lv_label_set_text(s_game_hint,
        game.calibrating ? "Calibrating... keep still" :
        (game.imu_ok ? "Press BOOT to reset center" : "IMU not available"));

    imu_motion_snapshot_t motion;
    imu_motion_get_snapshot(&motion);
    char motion_value[8];
    snprintf(motion_value, sizeof(motion_value), "%u%%", motion.intensity);
    lv_label_set_text(s_motion_value, motion_value);
    char acceleration[40];
    snprintf(acceleration, sizeof(acceleration), "LINEAR ACCEL  %.2f m/s2",
             motion.acceleration);
    lv_label_set_text(s_motion_accel, acceleration);
    lv_bar_set_value(s_motion_bar, motion.intensity, LV_ANIM_OFF);
    lv_label_set_text(s_motion_start_label, motion.running ? "STOP" : "START");
    lv_label_set_text(s_motion_state,
        !motion.imu_ok ? "IMU NOT AVAILABLE" :
        !motion.running ? "STOPPED" :
        motion.moving ? "MOVING - SENDING OUTPUT" :
                        "STILL - NO REPEATED COMMANDS");
    lv_obj_set_style_text_color(s_motion_state,
        lv_color_hex(motion.moving ? COLOR_GREEN : COLOR_MUTED), 0);

    pattern_snapshot_t pattern;
    pattern_control_get_snapshot(&pattern);
    char pattern_value[12];
    snprintf(pattern_value, sizeof(pattern_value), "%u%%", pattern.intensity);
    lv_label_set_text(s_pattern_value, pattern_value);
    lv_label_set_text(s_pattern_start_label, pattern.running ? "STOP" : "START");
    for (int i = 0; i < PATTERN_COUNT; ++i) {
        lv_obj_set_style_bg_color(s_pattern_buttons[i],
            lv_color_hex(i == pattern.selected ? COLOR_MAGENTA : COLOR_CARD_2), 0);
    }

    power_status_snapshot_t power;
    power_status_get(&power);
    char battery[32];
    if (!power.available) {
        snprintf(battery, sizeof(battery), "UNAVAILABLE");
        lv_bar_set_value(s_battery_bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_text_color(s_battery_label,
                                    lv_color_hex(COLOR_MUTED), 0);
    } else if (!power.battery_connected) {
        snprintf(battery, sizeof(battery), "NO BATTERY");
        lv_bar_set_value(s_battery_bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_text_color(s_battery_label,
                                    lv_color_hex(COLOR_MUTED), 0);
    } else {
        snprintf(battery, sizeof(battery), "%u%%  %s", power.percent,
                 power.charging ? "CHARGING" : "BATTERY");
        lv_bar_set_value(s_battery_bar, power.percent, LV_ANIM_OFF);
        lv_obj_set_style_text_color(s_battery_label,
                                    lv_color_hex(COLOR_GREEN), 0);
    }
    lv_label_set_text(s_battery_label, battery);
}

void app_ui_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    set_bg(screen, COLOR_BG);

    create_home_view(screen);
    create_ble_view(screen);
    create_control_view(screen);
    create_game_view(screen);
    create_motion_view(screen);
    create_settings_view(screen);
    create_pattern_view(screen);

    show_view(s_home_view);
    lv_timer_create(ui_timer_cb, 80, NULL);
}
