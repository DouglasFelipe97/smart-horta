#include "app_ambience_control.h"

#include <stdbool.h>
#include <stdint.h>
#include "app_pcf8574.h"
#include "app_pin_config.h"
#include "esp_log.h"

#define APP_AMBIENCE_TRIGGER_DELAY_MS 30000U
#define APP_AMBIENCE_TEMPERATURE_LIMIT_C 5.0f
#define APP_AMBIENCE_HUMIDITY_LIMIT_PERCENT 5.0f
#define APP_AMBIENCE_TEMPERATURE_HYSTERESIS_C 1.0f
#define APP_AMBIENCE_HUMIDITY_HYSTERESIS_PERCENT 1.0f
#define APP_AMBIENCE_OUTPUT_RETRY_MS 1000U

typedef enum {
    APP_AMBIENCE_CONDITION_AIR_HOT,
    APP_AMBIENCE_CONDITION_AIR_DRY,
    APP_AMBIENCE_CONDITION_AIR_HUMID,
    APP_AMBIENCE_CONDITION_SOIL_DRY,
    APP_AMBIENCE_CONDITION_COUNT,
} app_ambience_condition_t;

typedef struct {
    float air_temperature;
    float air_humidity;
    float soil_humidity;
} app_ambience_targets_t;

static const char *TAG_AMBIENCE_CONTROL = "AMBIENCE_CONTROL";
static bool s_condition_active[APP_AMBIENCE_CONDITION_COUNT];
static uint32_t s_condition_started_ms[APP_AMBIENCE_CONDITION_COUNT];
static uint8_t s_applied_output_mask;
static uint32_t s_last_output_attempt_ms[APP_OUTPUT_COUNT];

static const app_ambience_curve_t *curve_for_age(const app_state_snapshot_t *snapshot,
                                                  uint32_t age_days,
                                                  uint8_t *curve_index)
{
    uint8_t index = 0;
    while (index + 1U < APP_AMBIENCE_CURVE_COUNT &&
           age_days >= snapshot->ambience_curves[index + 1U].age_days) {
        index++;
    }
    *curve_index = index;
    return &snapshot->ambience_curves[index];
}

static float interpolate_value(float first, float second, uint32_t age,
                               uint16_t first_age, uint16_t second_age)
{
    if (second_age <= first_age || age <= first_age) {
        return first;
    }
    if (age >= second_age) {
        return second;
    }
    float progress = (float)(age - first_age) / (float)(second_age - first_age);
    return first + ((second - first) * progress);
}

static app_ambience_targets_t targets_for_age(const app_state_snapshot_t *snapshot,
                                               uint32_t age_days,
                                               uint8_t *curve_index)
{
    const app_ambience_curve_t *current = curve_for_age(snapshot, age_days, curve_index);
    app_ambience_targets_t targets = {
        .air_temperature = (float)current->air_temperature_x10 / 10.0f,
        .air_humidity = current->air_humidity_percent,
        .soil_humidity = current->soil_humidity_percent,
    };

    if (*curve_index + 1U < APP_AMBIENCE_CURVE_COUNT) {
        const app_ambience_curve_t *next = &snapshot->ambience_curves[*curve_index + 1U];
        targets.air_temperature = interpolate_value(
            targets.air_temperature,
            (float)next->air_temperature_x10 / 10.0f,
            age_days,
            current->age_days,
            next->age_days
        );
        targets.air_humidity = interpolate_value(
            targets.air_humidity,
            next->air_humidity_percent,
            age_days,
            current->age_days,
            next->age_days
        );
        targets.soil_humidity = interpolate_value(
            targets.soil_humidity,
            next->soil_humidity_percent,
            age_days,
            current->age_days,
            next->age_days
        );
    }
    return targets;
}

static uint8_t count_for_deviation(uint8_t configured_count, float deviation)
{
    if (configured_count == 0U) {
        return 0U;
    }
    uint8_t requested = (uint8_t)(deviation > 0.0f ? deviation : 0.0f) + 1U;
    return requested < configured_count ? requested : configured_count;
}

static void set_group_outputs(bool *desired, uint8_t first_output, uint8_t count)
{
    for (uint8_t index = 0; index < count; index++) {
        desired[first_output + index] = true;
    }
}

static void apply_outputs(const bool desired[APP_OUTPUT_COUNT], uint32_t now_ms)
{
    for (uint8_t output = 0; output < APP_OUTPUT_COUNT; output++) {
        uint8_t output_mask = (uint8_t)(1U << output);
        bool currently_active = (s_applied_output_mask & output_mask) != 0U;
        if (currently_active == desired[output] ||
            now_ms - s_last_output_attempt_ms[output] < APP_AMBIENCE_OUTPUT_RETRY_MS) {
            continue;
        }

        s_last_output_attempt_ms[output] = now_ms;
        bool electrical_high = desired[output]
            ? (APP_PCF8574_ACTIVE_HIGH != 0)
            : (APP_PCF8574_ACTIVE_HIGH == 0);
        esp_err_t err = app_pcf8574_write_output((app_pcf8574_output_t)output, electrical_high);
        if (err == ESP_OK) {
            if (desired[output]) {
                s_applied_output_mask |= output_mask;
            } else {
                s_applied_output_mask &= (uint8_t)~output_mask;
            }
        } else {
            ESP_LOGW(TAG_AMBIENCE_CONTROL, "Falha ao comandar saida %u: %s",
                     (unsigned)output, esp_err_to_name(err));
        }
    }
}

void app_ambience_control_update(const app_state_snapshot_t *snapshot,
                                 uint32_t now_ms,
                                 bool sensor_monitor_active)
{
    if (snapshot == NULL) {
        return;
    }

    bool desired_outputs[APP_OUTPUT_COUNT] = {false};
    bool trigger[APP_AMBIENCE_CONDITION_COUNT] = {false};
    bool hold[APP_AMBIENCE_CONDITION_COUNT] = {false};
    float max_deviation[APP_AMBIENCE_CONDITION_COUNT] = {0.0f};
    uint8_t curve_index = 0;
    app_ambience_targets_t targets = targets_for_age(snapshot, snapshot->lot_age_days, &curve_index);
    const app_ambience_curve_t *curve = &snapshot->ambience_curves[curve_index];

    if (snapshot->ambience_configured && sensor_monitor_active &&
        snapshot->lot_status == APP_LOT_STATUS_ACTIVE) {
        for (uint8_t probe = 0; probe < APP_PROBE_COUNT; probe++) {
            const app_probe_snapshot_t *reading = &snapshot->probes[probe];
            if (!reading->installed || !reading->has_reading) {
                continue;
            }

            float hot_limit = targets.air_temperature + APP_AMBIENCE_TEMPERATURE_LIMIT_C;
            float hot_clear = hot_limit - APP_AMBIENCE_TEMPERATURE_HYSTERESIS_C;
            if (reading->air_temperature >= hot_limit) {
                trigger[APP_AMBIENCE_CONDITION_AIR_HOT] = true;
                float deviation = reading->air_temperature - hot_limit;
                if (deviation > max_deviation[APP_AMBIENCE_CONDITION_AIR_HOT]) {
                    max_deviation[APP_AMBIENCE_CONDITION_AIR_HOT] = deviation;
                }
            }
            if (reading->air_temperature > hot_clear) {
                hold[APP_AMBIENCE_CONDITION_AIR_HOT] = true;
            }

            float air_dry_limit = targets.air_humidity - APP_AMBIENCE_HUMIDITY_LIMIT_PERCENT;
            float air_dry_clear = air_dry_limit + APP_AMBIENCE_HUMIDITY_HYSTERESIS_PERCENT;
            if (reading->air_humidity <= air_dry_limit) {
                trigger[APP_AMBIENCE_CONDITION_AIR_DRY] = true;
                float deviation = air_dry_limit - reading->air_humidity;
                if (deviation > max_deviation[APP_AMBIENCE_CONDITION_AIR_DRY]) {
                    max_deviation[APP_AMBIENCE_CONDITION_AIR_DRY] = deviation;
                }
            }
            if (reading->air_humidity < air_dry_clear) {
                hold[APP_AMBIENCE_CONDITION_AIR_DRY] = true;
            }

            float air_humid_limit = targets.air_humidity + APP_AMBIENCE_HUMIDITY_LIMIT_PERCENT;
            float air_humid_clear = air_humid_limit - APP_AMBIENCE_HUMIDITY_HYSTERESIS_PERCENT;
            if (reading->air_humidity >= air_humid_limit) {
                trigger[APP_AMBIENCE_CONDITION_AIR_HUMID] = true;
                float deviation = reading->air_humidity - air_humid_limit;
                if (deviation > max_deviation[APP_AMBIENCE_CONDITION_AIR_HUMID]) {
                    max_deviation[APP_AMBIENCE_CONDITION_AIR_HUMID] = deviation;
                }
            }
            if (reading->air_humidity > air_humid_clear) {
                hold[APP_AMBIENCE_CONDITION_AIR_HUMID] = true;
            }

            float soil_dry_limit = targets.soil_humidity - APP_AMBIENCE_HUMIDITY_LIMIT_PERCENT;
            float soil_dry_clear = soil_dry_limit + APP_AMBIENCE_HUMIDITY_HYSTERESIS_PERCENT;
            if (reading->soil_humidity <= soil_dry_limit) {
                trigger[APP_AMBIENCE_CONDITION_SOIL_DRY] = true;
                float deviation = soil_dry_limit - reading->soil_humidity;
                if (deviation > max_deviation[APP_AMBIENCE_CONDITION_SOIL_DRY]) {
                    max_deviation[APP_AMBIENCE_CONDITION_SOIL_DRY] = deviation;
                }
            }
            if (reading->soil_humidity < soil_dry_clear) {
                hold[APP_AMBIENCE_CONDITION_SOIL_DRY] = true;
            }
        }
    }

    for (uint8_t condition = 0; condition < APP_AMBIENCE_CONDITION_COUNT; condition++) {
        if (s_condition_active[condition] && !hold[condition]) {
            s_condition_active[condition] = false;
        } else if (!s_condition_active[condition] && trigger[condition]) {
            s_condition_active[condition] = true;
            s_condition_started_ms[condition] = now_ms;
        }
    }

    bool alarm_active = false;
    uint8_t active_count = 0;
    for (uint8_t condition = 0; condition < APP_AMBIENCE_CONDITION_COUNT; condition++) {
        if (!s_condition_active[condition]) {
            continue;
        }
        alarm_active = true;
        if (now_ms - s_condition_started_ms[condition] < APP_AMBIENCE_TRIGGER_DELAY_MS) {
            continue;
        }

        if (condition == APP_AMBIENCE_CONDITION_AIR_HOT) {
            active_count = count_for_deviation(curve->exhaust_fan_count,
                                               max_deviation[condition]);
            set_group_outputs(desired_outputs, APP_OUTPUT_EXHAUST_FAN_1, active_count);
        } else if (condition == APP_AMBIENCE_CONDITION_AIR_DRY) {
            active_count = count_for_deviation(curve->sprinkler_count,
                                               max_deviation[condition]);
            set_group_outputs(desired_outputs, APP_OUTPUT_SPRINKLER_1, active_count);
        } else if (condition == APP_AMBIENCE_CONDITION_AIR_HUMID) {
            active_count = count_for_deviation(curve->exhaust_fan_count,
                                               max_deviation[condition]);
            set_group_outputs(desired_outputs, APP_OUTPUT_EXHAUST_FAN_1, active_count);
        } else {
            active_count = count_for_deviation(curve->irrigator_count,
                                               max_deviation[condition]);
            set_group_outputs(desired_outputs, APP_OUTPUT_IRRIGATOR_1, active_count);
        }
    }

    app_model_set_ambience_alarm_active(alarm_active);
    apply_outputs(desired_outputs, now_ms);
}