/*
 * Application 4 — Synchronization Quest (Part B of Quest 1)
 *
 * Scaffold level: ~70% complete.
 *
 * Scaffold Code - AI useage:
 *   Addition of the USE_PI_MUTEX compile-time switch and the H/M/L
 *     priority-inversion harness (lock plumbing + timestamp telemetry)
 *   Logic to allow for switching the lock primitive between an inheriting
 *     mutex and a non-inheriting binary semaphore
 *   Commenting of code including human readable summaries
 *
 * What this scaffold gives you (the baseline COMPILES and BEHAVES):
 *   - Binary semaphore — signals from ISR to a responder task
 *   - Counting semaphore — manages a pool of resources (3 slots, 4 consumers)
 *   - Mutex — protects a shared variable that two tasks modify
 *   - A built-in priority-inversion demo (tasks H/M/L) whose lock primitive is
 *     selected by one #define, so "show it both ways" is a flag flip rather
 *     than a manual swap. The H block->acquire wait is measured and logged.
 *
 * What you do:
 *   1. Refactor each primitive into its ROLE-APPROPRIATE use (justify in README).
 *   2. Theme the responder / pool / writer names, log strings, and resource.
 *   3. Implement the induced-failure section (remove one primitive, observe).
 *   4. Run the inversion demo in BOTH modes (flip USE_PI_MUTEX), quote the two
 *      H-wait numbers, and walk the timeline in your README.
 *
 * What you DON'T need to change:
 *   - The ISR, the debounce gate, or the three primitive create calls.
 *   - The H/M/L lock plumbing or the timestamp telemetry — just read the numbers
 *     it prints. Tune only the *_ITERS / *_DELAY_MS knobs if you want a cleaner
 *     separation between the two modes.
 *
 * ============================================================
 *  LOCK MODE  (priority-inversion demo)
 * ============================================================
 *
 * USE_PI_MUTEX selects the lock type shared by tasks H and L. Both modes run the
 * SAME H/M/L scenario and log the SAME fields (H's block->acquire wait, plus the
 * L and M timeline); only the lock primitive differs.
 *
 *   USE_PI_MUTEX = 1  -> H and L share a FreeRTOS MUTEX (priority inheritance
 *                        ON). When H blocks on the lock L holds, L inherits H's
 *                        priority, so M cannot preempt L. H waits about L's
 *                        remaining critical section — bounded.
 *   USE_PI_MUTEX = 0  -> H and L share a BINARY SEMAPHORE used as a lock (no
 *                        ownership, no inheritance). M preempts L while L still
 *                        holds the lock, so H waits for M to finish too — the
 *                        classic unbounded priority inversion.
 *
 * The separation only appears because L's critical section is CPU-bound (a fixed
 * iteration burn). If L merely slept, the CPU would be free, M would run in both
 * modes, and the two numbers would converge — which is why the demo burns cycles
 * instead of calling vTaskDelay inside the lock.
 *
 * ============================================================
 * Theme: AVIATION
 * ============================================================
 */

#ifndef USE_PI_MUTEX
#define USE_PI_MUTEX 0
#endif

/* Set to 1 only for the induced-failure run (mutex removed). */
#ifndef INDUCE_MUTEX_FAILURE
#define INDUCE_MUTEX_FAILURE 1
#endif

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_task_wdt.h"

#define BUTTON_GPIO GPIO_NUM_18
#define HEARTBEAT_GPIO GPIO_NUM_2
#define DASHBOARD_PERIOD_MS 2000
#define BAR_WIDTH 20

static const char *TAG = "aviation";

/* ---------- Synchronization primitives ---------- */
static SemaphoreHandle_t sig_sem;       /* binary — ISR → responder */
static SemaphoreHandle_t pool_sem;      /* counting — N=3 (resource pool) */
static SemaphoreHandle_t shared_mux;    /* mutex — protect shared_state */

/* Shared state guarded by shared_mux */
static int shared_state = 0;

/* ---------- Live dashboard telemetry ----------
 * These counters are intentionally lightweight so the visualization does not
 * change the synchronization behavior being demonstrated. */
static volatile uint32_t stat_isr_pulses = 0;
static volatile uint32_t stat_responses = 0;
static volatile uint32_t stat_pool_acquires[4] = {0};
static volatile uint32_t stat_pool_timeouts[4] = {0};
static volatile uint32_t stat_writer_updates[2] = {0};
static volatile int stat_pool_busy = 0;
static volatile int stat_pool_peak = 0;
static volatile int64_t stat_pi_h_wait_us = -1;
static volatile int64_t stat_pi_l_hold_us = -1;
static volatile int64_t stat_pi_m_run_us = -1;

static TaskHandle_t responder_handle = NULL;
static TaskHandle_t pool_handles[4] = {NULL};
static TaskHandle_t writer_handles[2] = {NULL};

static void make_bar(char *out, size_t out_size, int value, int maximum)
{
    if (out_size < BAR_WIDTH + 3) return;
    if (maximum <= 0) maximum = 1;
    if (value < 0) value = 0;
    if (value > maximum) value = maximum;

    int filled = (value * BAR_WIDTH + maximum / 2) / maximum;
    out[0] = '[';
    for (int i = 0; i < BAR_WIDTH; ++i) out[i + 1] = (i < filled) ? '#' : '-';
    out[BAR_WIDTH + 1] = ']';
    out[BAR_WIDTH + 2] = '\0';
}

static void update_peak_busy(int busy)
{
    int peak = __atomic_load_n(&stat_pool_peak, __ATOMIC_RELAXED);
    while (busy > peak &&
           !__atomic_compare_exchange_n(&stat_pool_peak, &peak, busy, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        /* peak is refreshed by compare_exchange */
    }
}


static volatile int64_t last_edge_us;
static void IRAM_ATTR button_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (now - last_edge_us < 200) return;       /* debounce */
    last_edge_us = now;

    BaseType_t woken = pdFALSE;
    stat_isr_pulses++;
    xSemaphoreGiveFromISR(sig_sem, &woken);
    portYIELD_FROM_ISR(woken);
}


static void responder_task(void *arg)
{
    for (;;) {
        if (xSemaphoreTake(sig_sem, portMAX_DELAY) == pdTRUE) {
            stat_responses++;
            ESP_LOGI(TAG, "[RADAR] pulse detected — collision monitor notified");
        }
    }
}


static void pool_consumer_task(void *arg)
{
    int id = (int)(uintptr_t)arg;
    for (;;) {
        if (xSemaphoreTake(pool_sem, pdMS_TO_TICKS(1000)) == pdTRUE) {
            stat_pool_acquires[id - 1]++;
            int busy = __atomic_add_fetch(&stat_pool_busy, 1, __ATOMIC_RELAXED);
            update_peak_busy(busy);
            ESP_LOGI(TAG, "[RADIO#%d] acquired one of 3 communication channels", id);
            vTaskDelay(pdMS_TO_TICKS(500 + (id * 200)));   /* simulated work */
            __atomic_sub_fetch(&stat_pool_busy, 1, __ATOMIC_RELAXED);
            xSemaphoreGive(pool_sem);
            ESP_LOGI(TAG, "[RADIO#%d] released communication channel", id);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            stat_pool_timeouts[id - 1]++;
            ESP_LOGW(TAG, "[RADIO#%d] no channel available in 1s — transmission delayed", id);
        }
    }
}

/* ---------- Two tasks racing on shared_state — guarded by mutex ---------- */
static void shared_writer_task(void *arg)
{
    int id = (int)(uintptr_t)arg;
    for (;;) {
#if !INDUCE_MUTEX_FAILURE
        if (xSemaphoreTake(shared_mux, portMAX_DELAY) == pdTRUE) {
#endif
            int old = shared_state;
#if INDUCE_MUTEX_FAILURE
            taskYIELD();
#endif
            shared_state = old + 1;
            stat_writer_updates[id - 1]++;
            ESP_LOGI(TAG, "[FLIGHT-DATA#%d] update counter %d -> %d",
                     id, old, shared_state);
#if !INDUCE_MUTEX_FAILURE
            xSemaphoreGive(shared_mux);
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(150 + (id * 73)));    /* irregular */
    }
}

/* ============================================================
 *  Priority-inversion demo  (tasks H / M / L)
 * ============================================================
 *
 * Classic three-task inversion on one core:
 *   - L (low,  prio 5)  grabs the lock and runs a long CPU-bound section.
 *   - H (high, prio 15) tries the lock shortly after and blocks on it.
 *   - M (mid,  prio 10) becomes ready a little later and burns CPU. It does NOT
 *     touch the lock — it is pure interference.
 *
 * MUTEX mode  (USE_PI_MUTEX=1): when H blocks, L inherits prio 15, so M cannot
 *   preempt L; L finishes its section and hands the lock to H. H's wait is
 *   bounded by L's remaining critical section.
 * BINARY-SEM mode (USE_PI_MUTEX=0): L stays at prio 5; M preempts L while L holds
 *   the lock, so H waits for M to drain too. The wait inflates by M's run time.
 *
 * Read the "[PI][H] ... waited N us" line in each mode; that delta is the lesson.
 */
#if USE_PI_MUTEX
#define PI_LOCK_CREATE() xSemaphoreCreateMutex()
#define PI_LOCK_NAME     "MUTEX (priority inheritance ON)"
#else
#define PI_LOCK_CREATE() xSemaphoreCreateBinary()
#define PI_LOCK_NAME     "BINARY SEM (no inheritance)"
#endif

static SemaphoreHandle_t pi_lock;

/* Stagger knobs — control the ordering, not the durations. */
#define PI_H_DELAY_MS  50      /* H tries the lock 50 ms after start (after L holds it) */
#define PI_M_DELAY_MS  100     /* M becomes ready 100 ms after start */

/* Work knobs — fixed-iteration CPU burns. TUNE on Wokwi using the logged
 * wall-clock durations: aim for L ~500 ms and M ~1000 ms when each runs alone.
 * Absolute values do not affect WHICH mode wins; they set how large the gap is. */
#define PI_L_ITERS  20000000UL
#define PI_M_ITERS  40000000UL

static volatile uint32_t pi_sink;       /* defeats dead-code elimination */
static void pi_burn(uint32_t iters)
{
    uint32_t x = pi_sink ? pi_sink : 1u;
    for (uint32_t i = 0; i < iters; i++) { x ^= (x << 5); x += i; }
    pi_sink = x;
}

static void pi_low_task(void *arg)
{
    /* L is created last in app_main, so it grabs the lock immediately. */
    xSemaphoreTake(pi_lock, portMAX_DELAY);
    int64_t t_acq = esp_timer_get_time();
    ESP_LOGI(TAG, "[PI][L] took lock @ %lld us — entering CPU-bound section",
             (long long)t_acq);
    pi_burn(PI_L_ITERS);
    int64_t t_rel = esp_timer_get_time();
    xSemaphoreGive(pi_lock);
    stat_pi_l_hold_us = t_rel - t_acq;
    ESP_LOGI(TAG, "[PI][L] released lock @ %lld us (held %lld us wall-clock)",
             (long long)t_rel, (long long)(t_rel - t_acq));
    vTaskDelete(NULL);
}

static void pi_med_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(PI_M_DELAY_MS));
    int64_t t0 = esp_timer_get_time();
    ESP_LOGI(TAG, "[PI][M] ready @ %lld us — burning CPU (takes no lock)",
             (long long)t0);
    pi_burn(PI_M_ITERS);
    int64_t t1 = esp_timer_get_time();
    stat_pi_m_run_us = t1 - t0;
    ESP_LOGI(TAG, "[PI][M] done  @ %lld us (ran %lld us wall-clock)",
             (long long)t1, (long long)(t1 - t0));
    vTaskDelete(NULL);
}

static void pi_high_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(PI_H_DELAY_MS));
    int64_t t_block = esp_timer_get_time();
    ESP_LOGI(TAG, "[PI][H] wants lock @ %lld us — blocking", (long long)t_block);
    xSemaphoreTake(pi_lock, portMAX_DELAY);
    int64_t t_acq = esp_timer_get_time();
    int64_t wait = t_acq - t_block;
    stat_pi_h_wait_us = wait;
    ESP_LOGW(TAG, "[PI][H] ACQUIRED @ %lld us — waited %lld us (~%lld ms)  [lock=%s]",
             (long long)t_acq, (long long)wait, (long long)(wait / 1000), PI_LOCK_NAME);
    xSemaphoreGive(pi_lock);
    vTaskDelete(NULL);
}

static void start_inversion_demo(void)
{
    pi_lock = PI_LOCK_CREATE();
#if !USE_PI_MUTEX
    /* A binary semaphore is created empty; prime it once so it starts "unlocked". */
    xSemaphoreGive(pi_lock);
#endif
    ESP_LOGI(TAG, "[PI] inversion demo lock = %s", PI_LOCK_NAME);

    /* Create H and M first (they delay before acting), then L LAST so L wins the
     * lock the instant it is created instead of starving app_main while it burns. */
    xTaskCreatePinnedToCore(pi_high_task, "H", 4096, NULL, 15, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(pi_med_task,  "M", 4096, NULL, 10, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(pi_low_task,  "L", 4096, NULL,  5, NULL, APP_CPU_NUM);
}

/* ---------- Live resource-utilization dashboard ---------- */
static void dashboard_task(void *arg)
{
    uint32_t previous_acquires[4] = {0};
    uint32_t previous_updates[2] = {0};
    uint32_t previous_isr = 0;
    bool led_on = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(DASHBOARD_PERIOD_MS));
        led_on = !led_on;
        gpio_set_level(HEARTBEAT_GPIO, led_on);

        int busy = __atomic_load_n(&stat_pool_busy, __ATOMIC_RELAXED);
        int free_channels = (int)uxSemaphoreGetCount(pool_sem);
        int peak = __atomic_load_n(&stat_pool_peak, __ATOMIC_RELAXED);
        uint32_t total_attempts = stat_writer_updates[0] + stat_writer_updates[1];
        int lost_updates = (int)total_attempts - shared_state;
        if (lost_updates < 0) lost_updates = 0;

        char pool_bar[BAR_WIDTH + 3];
        char radio_bar[4][BAR_WIDTH + 3];
        char writer_bar[2][BAR_WIDTH + 3];
        make_bar(pool_bar, sizeof(pool_bar), busy, 3);

        uint32_t radio_delta[4];
        uint32_t writer_delta[2];
        uint32_t max_radio_delta = 1;
        uint32_t max_writer_delta = 1;
        for (int i = 0; i < 4; ++i) {
            radio_delta[i] = stat_pool_acquires[i] - previous_acquires[i];
            previous_acquires[i] = stat_pool_acquires[i];
            if (radio_delta[i] > max_radio_delta) max_radio_delta = radio_delta[i];
        }
        for (int i = 0; i < 2; ++i) {
            writer_delta[i] = stat_writer_updates[i] - previous_updates[i];
            previous_updates[i] = stat_writer_updates[i];
            if (writer_delta[i] > max_writer_delta) max_writer_delta = writer_delta[i];
        }
        for (int i = 0; i < 4; ++i)
            make_bar(radio_bar[i], sizeof(radio_bar[i]), radio_delta[i], max_radio_delta);
        for (int i = 0; i < 2; ++i)
            make_bar(writer_bar[i], sizeof(writer_bar[i]), writer_delta[i], max_writer_delta);

        uint32_t isr_delta = stat_isr_pulses - previous_isr;
        previous_isr = stat_isr_pulses;

        printf("\n+================================================================+\n");
        printf("|              AVIATION RESOURCE UTILIZATION DASHBOARD           |\n");
        printf("+================================================================+\n");
        printf("| Uptime: %8lld ms | Core: %d | Lock: %-24s |\n",
               (long long)(esp_timer_get_time() / 1000), xPortGetCoreID(),
               USE_PI_MUTEX ? "PI mutex" : "binary semaphore");
        printf("+----------------------------------------------------------------+\n");
        printf("| RADIO CHANNEL POOL                                             |\n");
        printf("| Busy %s %d/3 (%3d%%) | Free: %d | Peak: %d/3       |\n",
               pool_bar, busy, (busy * 100) / 3, free_channels, peak);
        printf("+----------------------------------------------------------------+\n");
        printf("| ACTIVITY DURING LAST %d ms                                     |\n", DASHBOARD_PERIOD_MS);
        for (int i = 0; i < 4; ++i) {
            printf("| Radio %d  %s %3lu acquisitions | timeouts: %-4lu |\n",
                   i + 1, radio_bar[i], (unsigned long)radio_delta[i],
                   (unsigned long)stat_pool_timeouts[i]);
        }
        for (int i = 0; i < 2; ++i) {
            printf("| Writer %d %s %3lu updates      | stack free: %-5u |\n",
                   i + 1, writer_bar[i], (unsigned long)writer_delta[i],
                   writer_handles[i] ? (unsigned)uxTaskGetStackHighWaterMark(writer_handles[i]) : 0);
        }
        printf("+----------------------------------------------------------------+\n");
        printf("| SYNCHRONIZATION HEALTH                                         |\n");
        printf("| Radar ISR events: %-6lu (+%-3lu) | handled: %-6lu             |\n",
               (unsigned long)stat_isr_pulses, (unsigned long)isr_delta,
               (unsigned long)stat_responses);
        printf("| Writer attempts: %-7lu | shared counter: %-7d | lost: %-5d |\n",
               (unsigned long)total_attempts, shared_state, lost_updates);
        printf("| Mutex protection: %-3s | race indicator: %-7s                  |\n",
               INDUCE_MUTEX_FAILURE ? "OFF" : "ON",
               lost_updates ? "WARNING" : "CLEAR");
        printf("+----------------------------------------------------------------+\n");
        printf("| PRIORITY INVERSION TELEMETRY                                   |\n");
        if (stat_pi_h_wait_us >= 0) {
            printf("| H wait: %8lld us | L hold: %8lld us | M run: %8lld us |\n",
                   (long long)stat_pi_h_wait_us,
                   (long long)stat_pi_l_hold_us,
                   (long long)stat_pi_m_run_us);
        } else {
            printf("| Demo still running; waiting for H to acquire the lock...       |\n");
        }
        printf("+----------------------------------------------------------------+\n");
        printf("| Heartbeat GPIO %-2d: %-3s | Responder stack free: %-5u          |\n",
               HEARTBEAT_GPIO, led_on ? "ON" : "OFF",
               responder_handle ? (unsigned)uxTaskGetStackHighWaterMark(responder_handle) : 0);
        printf("+================================================================+\n");
    }
}

/* ---------- app_main ---------- */
void app_main(void)
{
    esp_task_wdt_reconfigure(&(esp_task_wdt_config_t){.timeout_ms = 10000, .idle_core_mask = 0, .trigger_panic = false });
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "==== App 4 [AVIATION] starting — sync quest ====");
    ESP_LOGI(TAG, "Lock mode: %s (USE_PI_MUTEX=%d)", PI_LOCK_NAME, USE_PI_MUTEX);
    ESP_LOGI(TAG, "Induced mutex failure: %s",
             INDUCE_MUTEX_FAILURE ? "ON" : "OFF");

    gpio_set_direction(HEARTBEAT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(HEARTBEAT_GPIO, 0);

    sig_sem    = xSemaphoreCreateBinary();
    pool_sem   = xSemaphoreCreateCounting(3, 3);
    shared_mux = xSemaphoreCreateMutex();

    /* ISR + responder */
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&cfg);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL);

    xTaskCreatePinnedToCore(responder_task, "radar_resp", 4096, NULL, 12, &responder_handle, APP_CPU_NUM);

    /* Pool consumers — 4 contending for 3 slots */
    for (int i = 1; i <= 4; i++) {
        xTaskCreatePinnedToCore(pool_consumer_task, "radio", 4096,
                                (void*)(uintptr_t)i, 5, &pool_handles[i - 1], APP_CPU_NUM);
    }

    /* Shared-state writers — both update under the mutex */
    xTaskCreatePinnedToCore(shared_writer_task, "flight_data_1", 4096, (void*)1, 8, &writer_handles[0], APP_CPU_NUM);
    xTaskCreatePinnedToCore(shared_writer_task, "flight_data_2", 4096, (void*)2, 8, &writer_handles[1], APP_CPU_NUM);

    /* Priority-inversion demo (H/M/L). For the cleanest H-wait numbers, you can
     * temporarily comment out the pool/writer creation above so Core 1 carries
     * only this demo. */
    start_inversion_demo();

    /* Low-priority observer: visualization must not disturb the experiment. */
    xTaskCreatePinnedToCore(dashboard_task, "dashboard", 6144, NULL, 1, NULL, APP_CPU_NUM);
}
