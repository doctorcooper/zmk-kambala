#include <math.h>
#include <stdlib.h>

#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>
#include <zmk/workqueue.h>

#include "status_led.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// timeout
#define LED_BLINK_PROFILE_DELAY 200
#define LED_BLINK_CONN_DELAY 140
#define LED_FADE_DELAY 2
#define LED_BATTERY_SHOW_DELAY 700
#define LED_BATTERY_SLEEP_SHOW 1000
#define LED_BATTERY_BLINK_DELAY 200

#define LED_STATUS_ON 100
#define LED_STATUS_OFF 0

// animation options
#define disable_led_sleep_pc
// #define show_led_idle

// led array count
#define CHILD_COUNT(...) +1
#define DT_NUM_CHILD(node_id) (DT_FOREACH_CHILD(node_id, CHILD_COUNT))
#define BACKLIGHT_NUM_LEDS DT_NUM_CHILD(DT_CHOSEN(zmk_backlight))

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    bool check_conn_working = false;
#else
    bool peripheral_ble_connected = false;
#endif

static enum zmk_usb_conn_state usb_conn_state = ZMK_USB_CONN_NONE;

struct led {
    const struct device *dev;
    uint32_t id;
};

enum {
    BAT_1,
    BAT_2,
    BAT_3,
    BAT_4,
};

struct led pwm_leds[] = {
    [BAT_1] = {
        .dev = DEVICE_DT_GET(DT_CHOSEN(zmk_backlight)),
        .id = 0,
    },
    [BAT_2] = {
        .dev = DEVICE_DT_GET(DT_CHOSEN(zmk_backlight)),
        .id = 1,
    },
    [BAT_3] = {
        .dev = DEVICE_DT_GET(DT_CHOSEN(zmk_backlight)),
        .id = 2,
    },
    [BAT_4] = {
        .dev = DEVICE_DT_GET(DT_CHOSEN(zmk_backlight)),
        .id = 3,
    },
};

static K_MUTEX_DEFINE(led_mutex);
static bool soft_off_led_active;
static bool soft_off_led_animating;
static int64_t soft_off_led_started_at;
static uint32_t soft_off_led_hold_ms;

// Use a separate queue: battery/USB fades sleep on the system work queue.
static struct k_work_q soft_off_led_queue;
K_THREAD_STACK_DEFINE(soft_off_led_stack, 1024);

static void soft_off_led_render(void) {
    int64_t elapsed = k_uptime_get() - soft_off_led_started_at;
    bool ready = elapsed >= soft_off_led_hold_ms;
    for (int i = 0; i < BACKLIGHT_NUM_LEDS; i++) {
        int brightness;
        if (ready) {
            brightness = ((elapsed - soft_off_led_hold_ms) / 250) % 2 ? 25 : 100;
        } else {
            int64_t progress = elapsed * BACKLIGHT_NUM_LEDS * 100 / soft_off_led_hold_ms;
            brightness = CLAMP(progress - i * 100, 0, 100);
        }
        led_set_brightness(pwm_leds[i].dev, pwm_leds[i].id, brightness);
    }
}

static void soft_off_led_tick(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(soft_off_led_work, soft_off_led_tick);

static void soft_off_led_tick(struct k_work *work) {
    k_mutex_lock(&led_mutex, K_FOREVER);
    if (soft_off_led_animating) {
        soft_off_led_render();
        k_work_reschedule_for_queue(&soft_off_led_queue, &soft_off_led_work, K_MSEC(25));
    }
    k_mutex_unlock(&led_mutex);
}

void status_led_soft_off_start(uint32_t hold_ms) {
    k_mutex_lock(&led_mutex, K_FOREVER);
    soft_off_led_hold_ms = MAX(hold_ms, 1);
    soft_off_led_started_at = k_uptime_get();
    soft_off_led_active = true;
    soft_off_led_animating = true;
    soft_off_led_render();
    k_work_reschedule_for_queue(&soft_off_led_queue, &soft_off_led_work, K_NO_WAIT);
    k_mutex_unlock(&led_mutex);
}

void status_led_soft_off_stop(bool shutting_down) {
    k_mutex_lock(&led_mutex, K_FOREVER);
    soft_off_led_animating = false;
    // Keep other indications suppressed until power off has completed.
    soft_off_led_active = shutting_down;
    k_work_cancel_delayable(&soft_off_led_work);
    for (int i = 0; i < BACKLIGHT_NUM_LEDS; i++) {
        led_off(pwm_leds[i].dev, pwm_leds[i].id);
    }
    k_mutex_unlock(&led_mutex);
}

static bool status_led_set(const struct led *led, uint8_t brightness) {
    k_mutex_lock(&led_mutex, K_FOREVER);
    bool allowed = !soft_off_led_active;
    if (allowed) {
        led_set_brightness(led->dev, led->id, brightness);
    }
    k_mutex_unlock(&led_mutex);
    return allowed;
}

static void led_fade_ON(const struct led *led)
{
    for (int brightness = 0; brightness <= LED_STATUS_ON; brightness++)
    {
        if (!status_led_set(led, brightness)) {
            return;
        }
        k_msleep(LED_FADE_DELAY);
    }
    return;
}

static void led_fade_OFF(const struct led *led)
{
    for (int brightness = LED_STATUS_ON; brightness >= LED_STATUS_OFF; brightness--)
    {
        if (!status_led_set(led, brightness)) {
            return;
        }
        k_msleep(LED_FADE_DELAY);
    }
    return;
}

static void led_all_OFF() {
    for (int i = 0; i < BACKLIGHT_NUM_LEDS; i++) {
        const struct led *led = &pwm_leds[i];
        status_led_set(led, 0);
    }
    return;
}

void led_fade_blink(const struct led *led, uint32_t sleep_ms, const int count)
{
    for (int i = 0; i < count; i++)
    {
        led_fade_ON(led);
        k_msleep(sleep_ms);
        led_fade_OFF(led);
        k_msleep(sleep_ms);
    }
    return;
}

struct k_work_delayable check_ble_conn_work;

void check_ble_conn_handler(struct k_work *work)
{
    #if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (!check_conn_working)
    {
        return;
    } 
    else
    {   
        if (zmk_ble_active_profile_is_connected() || usb_conn_state != ZMK_USB_CONN_NONE)
        {
            check_conn_working = false;
            return;
        }
        else
        {
            uint8_t active_profile = zmk_ble_active_profile_index();
            if (active_profile < 3)
            {
                status_led_set(&pwm_leds[active_profile], LED_STATUS_ON);
            }
            led_fade_blink(&pwm_leds[3], LED_BLINK_CONN_DELAY, 1);
            led_all_OFF();
            k_work_schedule(&check_ble_conn_work, K_SECONDS(4)); // Restart work for next status check
            return;
        }
    }
    #else
        if (peripheral_ble_connected)
        {
            return;
        }
        else
        {
            led_fade_blink(&pwm_leds[3], LED_BLINK_CONN_DELAY, 1);
            led_all_OFF();
            k_work_schedule(&check_ble_conn_work, K_SECONDS(4)); // Restart work for next status check
            return;
        }
    #endif
}
K_WORK_DELAYABLE_DEFINE(check_ble_conn_work, check_ble_conn_handler);

void usb_animation_work_handler(struct k_work *work)
{
    #if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    #ifdef disable_led_sleep_pc
        if (usb_conn_state == USB_DC_SUSPEND)
        {
            led_all_OFF();
            return;
        }
    #endif
    #endif
    for (int i = 0; i < BACKLIGHT_NUM_LEDS; i++)
    {
        led_fade_ON(&pwm_leds[i]);
        k_msleep(LED_BATTERY_BLINK_DELAY / 2);
    }

    k_msleep(LED_BATTERY_BLINK_DELAY);

    for (int i = BACKLIGHT_NUM_LEDS - 1; i >= 0; i--)
    {
        led_fade_OFF(&pwm_leds[i]);
        k_msleep(LED_BATTERY_BLINK_DELAY / 2);
    }
    return;
}
K_WORK_DELAYABLE_DEFINE(usb_animation_work, usb_animation_work_handler);

void bat_animation_work_handler(struct k_work *work)
{
    uint8_t level = zmk_battery_state_of_charge();
    if (level != 0)
    {
        if (level <= 15)
        {
            led_fade_blink(&pwm_leds[0], LED_BATTERY_BLINK_DELAY, 3);
        }
        else
        {
            led_fade_ON(&pwm_leds[0]);
        }
        if (level > 30 && level <= 50)
        {
            led_fade_blink(&pwm_leds[1], LED_BATTERY_BLINK_DELAY, 3);
        }
        else if (level > 50)
        {
            led_fade_ON(&pwm_leds[1]);
        }
        if (level > 70 && level <= 90)
        {
            led_fade_blink(&pwm_leds[2], LED_BATTERY_BLINK_DELAY, 3);
        }
        else if (level > 90)
        {
            led_fade_ON(&pwm_leds[2]);
        }
        if (level == 100)
        {
            led_fade_OFF(&pwm_leds[0]);
            led_fade_OFF(&pwm_leds[1]);
            led_fade_OFF(&pwm_leds[2]);
            k_msleep(LED_BATTERY_BLINK_DELAY);
            led_fade_ON(&pwm_leds[0]);
            led_fade_ON(&pwm_leds[1]);
            led_fade_ON(&pwm_leds[2]);
            k_msleep(LED_BATTERY_BLINK_DELAY);
        }
        k_msleep(LED_BATTERY_SHOW_DELAY);
        led_all_OFF();
        k_work_schedule(&check_ble_conn_work, K_SECONDS(4));
    }
    return;
}
K_WORK_DELAYABLE_DEFINE(bat_animation_work, bat_animation_work_handler);

static int led_init(const struct device *dev)
{
    k_work_queue_start(&soft_off_led_queue, soft_off_led_stack,
                       K_THREAD_STACK_SIZEOF(soft_off_led_stack),
                       K_PRIO_PREEMPT(5), NULL);
    led_all_OFF();
    k_work_schedule(&bat_animation_work, K_SECONDS(1));
    return 0;
}

SYS_INIT(led_init, APPLICATION, 32);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
int ble_profile_listener(const zmk_event_t *eh)
{
    const struct zmk_ble_active_profile_changed *profile_ev = NULL;
    if ((profile_ev = as_zmk_ble_active_profile_changed(eh)) == NULL)
    {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (profile_ev->index <= 2)
    {
        led_fade_blink(&pwm_leds[profile_ev->index], LED_BLINK_PROFILE_DELAY, 1);
    }

    if (!check_conn_working)
    {
        check_conn_working = true;
        k_work_schedule(&check_ble_conn_work, K_SECONDS(4));
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(ble_profile_status, ble_profile_listener)
ZMK_SUBSCRIPTION(ble_profile_status, zmk_ble_active_profile_changed);

#else
int led_profile_listener(const zmk_event_t *eh)
{
    const struct zmk_split_peripheral_status_changed *status = as_zmk_split_peripheral_status_changed(eh);
    
    peripheral_ble_connected = status->connected;
    if (!peripheral_ble_connected) {
         k_work_schedule(&check_ble_conn_work, K_SECONDS(4));
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(led_profile_status, led_profile_listener)
ZMK_SUBSCRIPTION(led_profile_status, zmk_split_peripheral_status_changed);

#endif

int led_state_listener(const zmk_event_t *eh)
{
    enum zmk_activity_state state = zmk_activity_get_state();
    #if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        if (state == ZMK_ACTIVITY_ACTIVE && !check_conn_working)
        {
            check_conn_working = true;
            k_work_schedule(&check_ble_conn_work, K_SECONDS(4));
        }
    #else
        if (state == ZMK_ACTIVITY_ACTIVE)
        {
            peripheral_ble_connected = true;
            k_work_schedule(&check_ble_conn_work, K_SECONDS(4));
        }
    #endif

    // CONFIG_ZMK_IDLE_TIMEOUT Default 30sec
    #ifdef show_led_idle
    if (state != ZMK_ACTIVITY_ACTIVE)
    {
        led_bat_animation();
    }
else
    {
        led_all_OFF();
    }
    #endif
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(led_activity_state, led_state_listener)
ZMK_SUBSCRIPTION(led_activity_state, zmk_activity_state_changed);

int usb_conn_listener(const zmk_event_t *eh)
{
    const struct zmk_usb_conn_state_changed *usb_ev = NULL;
    if ((usb_ev = as_zmk_usb_conn_state_changed(eh)) == NULL)
    {
        return ZMK_EV_EVENT_BUBBLE;
    }
    
    usb_conn_state = usb_ev->conn_state;

    if (usb_conn_state == ZMK_USB_CONN_POWERED)
    {
        k_work_schedule(&usb_animation_work, K_NO_WAIT);
    }
    else
    {
        #if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
            check_conn_working = true;
        #else
            usb_conn_state = true;
        #endif
        k_work_schedule(&check_ble_conn_work, K_SECONDS(4));
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(usb_conn_state_listener, usb_conn_listener)
ZMK_SUBSCRIPTION(usb_conn_state_listener, zmk_usb_conn_state_changed);

void show_battery()
{
    k_work_schedule(&bat_animation_work, K_NO_WAIT);
}

void hide_battery()
{
    led_all_OFF();
}
