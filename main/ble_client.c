#include "ble_client.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "sosexy_ble";

typedef struct {
    ble_scan_result_t public_data;
    ble_addr_t address;
} scan_entry_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static scan_entry_t s_results[BLE_MAX_SCAN_RESULTS];
static size_t s_result_count;
static uint32_t s_generation;
static ble_app_state_t s_state = BLE_APP_STARTING;
static char s_peer_name[32];
static bool s_host_synced;
static bool s_scan_requested = true;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_write_handle;
static bool s_write_in_flight;
static motor_id_t s_in_flight_motor;
static uint8_t s_pending[MOTOR_COUNT];
static uint8_t s_source_values[BLE_CONTROL_SOURCE_COUNT][MOTOR_COUNT];
static uint8_t s_dirty_mask;
static TaskHandle_t s_command_task;

static const ble_uuid16_t s_write_uuid16 = BLE_UUID16_INIT(0xee03);
static const ble_uuid128_t s_write_uuid128 = BLE_UUID128_INIT(
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x03, 0xee, 0x00, 0x00);

static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void set_state(ble_app_state_t state)
{
    portENTER_CRITICAL(&s_lock);
    s_state = state;
    portEXIT_CRITICAL(&s_lock);
}

static void address_to_string(const ble_addr_t *addr, char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);
}

static bool uuid_is_write_char(const ble_uuid_t *uuid)
{
    return ble_uuid_cmp(uuid, &s_write_uuid16.u) == 0 ||
           ble_uuid_cmp(uuid, &s_write_uuid128.u) == 0;
}

static void add_scan_result(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields = {0};
    char name[32] = {0};

    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) == 0 &&
        fields.name != NULL && fields.name_len > 0) {
        size_t length = fields.name_len;
        if (length >= sizeof(name)) {
            length = sizeof(name) - 1;
        }
        memcpy(name, fields.name, length);
        name[length] = '\0';
    }

    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < s_result_count; ++i) {
        if (s_results[i].address.type == disc->addr.type &&
            memcmp(s_results[i].address.val, disc->addr.val, sizeof(disc->addr.val)) == 0) {
            s_results[i].public_data.rssi = disc->rssi;
            if (name[0] != '\0' && s_results[i].public_data.name[0] == '\0') {
                strlcpy(s_results[i].public_data.name, name, sizeof(s_results[i].public_data.name));
                ++s_generation;
            }
            portEXIT_CRITICAL(&s_lock);
            return;
        }
    }

    if (s_result_count < BLE_MAX_SCAN_RESULTS) {
        scan_entry_t *entry = &s_results[s_result_count++];
        memset(entry, 0, sizeof(*entry));
        entry->address = disc->addr;
        entry->public_data.rssi = disc->rssi;
        strlcpy(entry->public_data.name, name, sizeof(entry->public_data.name));
        address_to_string(&disc->addr, entry->public_data.address);
        ++s_generation;
    }
    portEXIT_CRITICAL(&s_lock);
}

static int characteristic_cb(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr,
                             void *arg)
{
    (void)arg;
    if (error->status == 0 && chr != NULL) {
        if (uuid_is_write_char(&chr->uuid.u)) {
            portENTER_CRITICAL(&s_lock);
            s_write_handle = chr->val_handle;
            portEXIT_CRITICAL(&s_lock);
            ESP_LOGI(TAG, "Found write characteristic at handle 0x%04x", chr->val_handle);
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE) {
        bool found;
        portENTER_CRITICAL(&s_lock);
        found = s_write_handle != 0;
        s_state = found ? BLE_APP_READY : BLE_APP_ERROR;
        portEXIT_CRITICAL(&s_lock);
        if (found) {
            ESP_LOGI(TAG, "SOSEXY control channel ready");
            ble_client_stop_all();
        } else {
            ESP_LOGE(TAG, "Connected device has no 0xEE03 write characteristic");
        }
        return 0;
    }

    ESP_LOGE(TAG, "Characteristic discovery failed: %d", error->status);
    set_state(BLE_APP_ERROR);
    return 0;
}

static int write_cb(uint16_t conn_handle,
                    const struct ble_gatt_error *error,
                    struct ble_gatt_attr *attr,
                    void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;

    motor_id_t completed_motor;
    portENTER_CRITICAL(&s_lock);
    completed_motor = s_in_flight_motor;
    if (error->status != 0) {
        s_dirty_mask |= (uint8_t)(1U << completed_motor);
    }
    s_write_in_flight = false;
    portEXIT_CRITICAL(&s_lock);
    if (error->status != 0) {
        ESP_LOGW(TAG, "GATT write failed for motor %d: %d",
                 (int)completed_motor, error->status);
    }
    if (s_command_task != NULL) {
        xTaskNotifyGive(s_command_task);
    }
    return 0;
}

static int start_scan_internal(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params params = {
        .itvl = 0x30,
        .window = 0x30,
        .filter_policy = 0,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 1,
    };

    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Cannot infer own address type: %d", rc);
        set_state(BLE_APP_ERROR);
        return rc;
    }

    ble_gap_disc_cancel();
    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc == 0) {
        set_state(BLE_APP_SCANNING);
        ESP_LOGI(TAG, "BLE scan started");
    } else {
        ESP_LOGE(TAG, "BLE scan failed: %d", rc);
        set_state(BLE_APP_ERROR);
    }
    return rc;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        add_scan_result(&event->disc);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGE(TAG, "Connection failed: %d", event->connect.status);
            portENTER_CRITICAL(&s_lock);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_write_handle = 0;
            s_state = BLE_APP_ERROR;
            portEXIT_CRITICAL(&s_lock);
            return 0;
        }

        portENTER_CRITICAL(&s_lock);
        s_conn_handle = event->connect.conn_handle;
        s_write_handle = 0;
        s_state = BLE_APP_DISCOVERING;
        portEXIT_CRITICAL(&s_lock);

        ESP_LOGI(TAG, "Connected; discovering GATT characteristics");
        struct ble_gap_upd_params fast_params = {
            .itvl_min = 6,   /* 7.5 ms */
            .itvl_max = 12,  /* 15 ms */
            .latency = 0,
            .supervision_timeout = 200,
            .min_ce_len = 0,
            .max_ce_len = 0,
        };
        int update_rc = ble_gap_update_params(event->connect.conn_handle,
                                              &fast_params);
        if (update_rc != 0) {
            ESP_LOGW(TAG, "Fast connection interval request failed: %d", update_rc);
        }
        if (ble_gattc_disc_all_chrs(event->connect.conn_handle, 1, 0xffff,
                                    characteristic_cb, NULL) != 0) {
            set_state(BLE_APP_ERROR);
        }
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc desc;
        if (event->conn_update.status == 0 &&
            ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "Connection interval updated to %.2f ms",
                     desc.conn_itvl * 1.25f);
        } else {
            ESP_LOGW(TAG, "Connection interval update status: %d",
                     event->conn_update.status);
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "Disconnected: %d", event->disconnect.reason);
        portENTER_CRITICAL(&s_lock);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_write_handle = 0;
        s_write_in_flight = false;
        s_dirty_mask = 0;
        s_peer_name[0] = '\0';
        s_state = BLE_APP_IDLE;
        portEXIT_CRITICAL(&s_lock);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_state == BLE_APP_SCANNING) {
            set_state(BLE_APP_IDLE);
        }
        return 0;

    default:
        return 0;
    }
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset: %d", reason);
    set_state(BLE_APP_ERROR);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "No usable BLE address: %d", rc);
        set_state(BLE_APP_ERROR);
        return;
    }

    portENTER_CRITICAL(&s_lock);
    s_host_synced = true;
    bool should_scan = s_scan_requested;
    s_scan_requested = false;
    if (!should_scan) {
        s_state = BLE_APP_IDLE;
    }
    portEXIT_CRITICAL(&s_lock);

    if (should_scan) {
        start_scan_internal();
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void command_task(void *arg)
{
    (void)arg;
    static const uint8_t mode_bytes[MOTOR_COUNT][2] = {
        {0x07, 0x08},
        {0x01, 0x02},
        {0x03, 0x04},
    };
    uint8_t next_motor = 0;

    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
        uint16_t write_handle = 0;
        motor_id_t motor = MOTOR_SUCTION;
        uint8_t intensity = 0;
        bool have_command = false;

        portENTER_CRITICAL(&s_lock);
        if (s_state == BLE_APP_READY && s_write_handle != 0 &&
            !s_write_in_flight && s_dirty_mask != 0) {
            for (int offset = 0; offset < MOTOR_COUNT; ++offset) {
                int i = (next_motor + offset) % MOTOR_COUNT;
                if ((s_dirty_mask & (1U << i)) != 0) {
                    motor = (motor_id_t)i;
                    intensity = s_pending[i];
                    s_dirty_mask &= (uint8_t)~(1U << i);
                    s_write_in_flight = true;
                    s_in_flight_motor = motor;
                    conn_handle = s_conn_handle;
                    write_handle = s_write_handle;
                    next_motor = (uint8_t)((i + 1) % MOTOR_COUNT);
                    have_command = true;
                    break;
                }
            }
        }
        portEXIT_CRITICAL(&s_lock);

        if (have_command) {
            uint8_t frame[12] = {
                0x01, 0x01, 0x00, 0x02, 0x00,
                mode_bytes[motor][0], 0x11, intensity,
                0x00, mode_bytes[motor][1], 0x11, 0x01,
            };
            int rc = ble_gattc_write_flat(conn_handle, write_handle,
                                          frame, sizeof(frame), write_cb, NULL);
            if (rc != 0) {
                portENTER_CRITICAL(&s_lock);
                s_dirty_mask |= (uint8_t)(1U << motor);
                s_write_in_flight = false;
                portEXIT_CRITICAL(&s_lock);
                vTaskDelay(pdMS_TO_TICKS(5));
                if (s_command_task != NULL) {
                    xTaskNotifyGive(s_command_task);
                }
            }
        }
    }
}

void ble_client_init(void)
{
    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %d", rc);
        set_state(BLE_APP_ERROR);
        return;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    xTaskCreate(command_task, "ble_commands", 4096, NULL, 5, &s_command_task);
}

bool ble_client_start_scan(void)
{
    portENTER_CRITICAL(&s_lock);
    s_result_count = 0;
    ++s_generation;
    bool ready = s_host_synced;
    if (!ready) {
        s_scan_requested = true;
        s_state = BLE_APP_STARTING;
    }
    portEXIT_CRITICAL(&s_lock);

    return !ready || start_scan_internal() == 0;
}

bool ble_client_connect_index(size_t index)
{
    ble_addr_t peer;
    char name[32];

    portENTER_CRITICAL(&s_lock);
    if (!s_host_synced || index >= s_result_count) {
        portEXIT_CRITICAL(&s_lock);
        return false;
    }
    peer = s_results[index].address;
    strlcpy(name, s_results[index].public_data.name, sizeof(name));
    portEXIT_CRITICAL(&s_lock);

    ble_gap_disc_cancel();
    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        return false;
    }

    portENTER_CRITICAL(&s_lock);
    strlcpy(s_peer_name, name[0] ? name : "Unnamed", sizeof(s_peer_name));
    s_state = BLE_APP_CONNECTING;
    portEXIT_CRITICAL(&s_lock);

    int rc = ble_gap_connect(own_addr_type, &peer, 10000, NULL, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Connect start failed: %d", rc);
        set_state(BLE_APP_ERROR);
        return false;
    }
    return true;
}

size_t ble_client_get_results(ble_scan_result_t *out, size_t capacity, uint32_t *generation)
{
    portENTER_CRITICAL(&s_lock);
    size_t count = s_result_count < capacity ? s_result_count : capacity;
    for (size_t i = 0; i < count; ++i) {
        out[i] = s_results[i].public_data;
    }
    if (generation != NULL) {
        *generation = s_generation;
    }
    portEXIT_CRITICAL(&s_lock);
    return count;
}

ble_app_state_t ble_client_get_state(char *peer_name, size_t peer_name_size)
{
    portENTER_CRITICAL(&s_lock);
    ble_app_state_t state = s_state;
    if (peer_name != NULL && peer_name_size > 0) {
        strlcpy(peer_name, s_peer_name, peer_name_size);
    }
    portEXIT_CRITICAL(&s_lock);
    return state;
}

bool ble_client_is_ready(void)
{
    return ble_client_get_state(NULL, 0) == BLE_APP_READY;
}

void ble_client_request_motor(motor_id_t motor, uint8_t intensity)
{
    ble_client_request_motor_from(BLE_CONTROL_MANUAL, motor, intensity);
}

void ble_client_request_motor_from(ble_control_source_t source,
                                   motor_id_t motor, uint8_t intensity)
{
    if (source >= BLE_CONTROL_SOURCE_COUNT || motor >= MOTOR_COUNT) {
        return;
    }
    if (intensity > 100) {
        intensity = 100;
    }
    portENTER_CRITICAL(&s_lock);
    s_source_values[source][motor] = intensity;
    uint8_t effective = 0;
    for (int i = 0; i < BLE_CONTROL_SOURCE_COUNT; ++i) {
        if (s_source_values[i][motor] > effective) {
            effective = s_source_values[i][motor];
        }
    }
    bool changed = s_pending[motor] != effective;
    if (changed) {
        s_pending[motor] = effective;
        s_dirty_mask |= (uint8_t)(1U << motor);
    }
    portEXIT_CRITICAL(&s_lock);
    if (changed && s_command_task != NULL) {
        xTaskNotifyGive(s_command_task);
    }
}

void ble_client_clear_source(ble_control_source_t source)
{
    if (source >= BLE_CONTROL_SOURCE_COUNT) {
        return;
    }
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    memset(s_source_values[source], 0, sizeof(s_source_values[source]));
    for (int motor = 0; motor < MOTOR_COUNT; ++motor) {
        uint8_t effective = 0;
        for (int i = 0; i < BLE_CONTROL_SOURCE_COUNT; ++i) {
            if (s_source_values[i][motor] > effective) {
                effective = s_source_values[i][motor];
            }
        }
        if (s_pending[motor] != effective) {
            s_pending[motor] = effective;
            s_dirty_mask |= (uint8_t)(1U << motor);
            changed = true;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    if (changed && s_command_task != NULL) {
        xTaskNotifyGive(s_command_task);
    }
}

void ble_client_stop_all(void)
{
    portENTER_CRITICAL(&s_lock);
    memset(s_source_values, 0, sizeof(s_source_values));
    for (int motor = 0; motor < MOTOR_COUNT; ++motor) {
        s_pending[motor] = 0;
        s_dirty_mask |= (uint8_t)(1U << motor);
    }
    portEXIT_CRITICAL(&s_lock);
    if (s_command_task != NULL) {
        xTaskNotifyGive(s_command_task);
    }
}
