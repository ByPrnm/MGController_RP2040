/**
 * Integrasi Pembangkit Sinyal PIO dengan Antarmuka LCD I2C
 *
 * Koneksi Perangkat Keras:
 * - LCD I2C: SDA -> GP4, SCL -> GP5
 * - Tombol: SELECT -> GP13, UP -> GP14, DOWN -> GP15
 * - Output Sinyal PIO: GP6, GP7, GP8, GP9
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "lib/lcd_i2c.h"
#include "signal_generator.pio.h"

// ===================== KONFIGURASI FLASH =====================
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CONFIG_MAGIC 0xDEADBEEF

typedef struct
{
    uint32_t magic;
    long frekuensi;
    long lebarPulsa;
    int waktuPerlakuan;
    long bedaFasa;
} ConfigData;

// ===================== KONFIGURASI PIN DAN LCD =====================
const uint8_t LCD_ADDRESS = 0x27;
const uint I2C_SDA_PIN = 4;
const uint I2C_SCL_PIN = 5;
i2c_inst_t *i2c_port = i2c0;

// Pin Tombol
const uint SELECT_BUTTON_PIN = 13;
const uint UP_BUTTON_PIN = 14;
const uint DOWN_BUTTON_PIN = 15;

// Pin Output PIO
const uint PIN_CH1_BASE = 6;

// ===================== VARIABEL UTAMA =====================
uint8_t menu = 1;
volatile long frekuensi = 100;
volatile long lebarPulsa = 3500;
volatile int waktuPerlakuan = 3;
volatile long bedaFasa = 100;
bool subMenu = false;

// ===================== VARIABEL PIO =====================
PIO pio = pio0;
uint sm, offset;
volatile bool process_complete = false;
int64_t alarm_callback(alarm_id_t id, void *user_data);

// ===================== DEBOUNCE BUTTON =====================
uint32_t last_press_time_select = 0, last_press_time_up = 0, last_press_time_down = 0;
uint8_t selectEvent = 0, upEvent = 0, downEvent = 0;
const uint DEBOUNCE_DELAY_MS = 50, HOLD_DELAY_MS = 500, REPEAT_DELAY_MS = 200;
bool select_pressed_flag = false, up_pressed_flag = false, down_pressed_flag = false;
bool select_held_flag = false, up_held_flag = false, down_held_flag = false;
uint32_t last_repeat_time_select = 0, last_repeat_time_up = 0, last_repeat_time_down = 0;

// ===================== PROTOTIPE FUNGSI =====================
void updateMenu();
void aturFrekuensi();
void aturLebarPulsa();
void aturWaktuPerlakuan();
void aturBedaFasa();
void handle_buttons();
void handle_menu();
void startPulseGeneration();
void stopPulseGeneration();
void load_parameters();
void save_parameters();
void init_pio_system();
void configure_pio_parameters(float freq_hz, float pulse_width_ns, float phase_shift_ns);
void calculate_delays(float sys_clk_hz, float pio_clk_div,
                      uint32_t *delay_A, uint32_t *delay_B,
                      uint32_t *delay_C, uint32_t *delay_D,
                      float freq_hz, float pulse_width_ns, float phase_shift_ns);

// ===================== ALARM CALLBACK =====================
int64_t alarm_callback(alarm_id_t id, void *user_data)
{
    // Hentikan state machine PIO
    pio_sm_set_enabled(pio, sm, false);
    process_complete = true;
    return 0; // Tidak perlu mengulang alarm
}

// ===================== FUNGSI TAMPILAN LCD =====================
void updateMenu()
{
    lcd_clear();
    char buf[17];

    switch (menu)
    {
    case 1:
        lcd_set_cursor(0, 0);
        lcd_string("FREKUENSI");
        lcd_set_cursor(1, 0);
        sprintf(buf, "%ld Hz", frekuensi);
        lcd_string(buf);
        break;
    case 2:
        lcd_set_cursor(0, 0);
        lcd_string("LEBAR PULSA");
        lcd_set_cursor(1, 0);
        if (lebarPulsa >= 1000)
            sprintf(buf, "%.1f uS", lebarPulsa / 1000.0);
        else
            sprintf(buf, "%ld nS", lebarPulsa);
        lcd_string(buf);
        break;
    case 3:
        lcd_set_cursor(0, 0);
        lcd_string("WAKTU PROSES");
        lcd_set_cursor(1, 0);
        sprintf(buf, "%d DETIK", waktuPerlakuan);
        lcd_string(buf);
        break;
    case 4:
        lcd_set_cursor(0, 0);
        lcd_string("BEDA FASA");
        lcd_set_cursor(1, 0);
        if (bedaFasa >= 1000)
            sprintf(buf, "%.1f uS", bedaFasa / 1000.0);
        else
            sprintf(buf, "%ld nS", bedaFasa);
        lcd_string(buf);
        break;
    case 5:
        lcd_set_cursor(0, 0);
        lcd_string(" MULAI PROSES ");
        break;
    case 6:
        lcd_set_cursor(0, 0);
        lcd_string("KOSONGKAN");
        lcd_set_cursor(1, 0);
        lcd_string("KAPASITOR BANK");
        break;
    }
}

void aturFrekuensi()
{
    lcd_clear();
    char buf[17];
    lcd_set_cursor(0, 0);
    lcd_string("SET FREKUENSI");
    lcd_set_cursor(1, 0);
    sprintf(buf, "%ld Hz ", frekuensi);
    lcd_string(buf);
}

void aturLebarPulsa()
{
    lcd_clear();
    char buf[17];
    lcd_set_cursor(0, 0);
    lcd_string("SET LEBAR PULSA");
    lcd_set_cursor(1, 0);
    if (lebarPulsa >= 1000)
        sprintf(buf, "%.1f uS ", lebarPulsa / 1000.0);
    else
        sprintf(buf, "%ld nS ", lebarPulsa);
    lcd_string(buf);
}

void aturWaktuPerlakuan()
{
    lcd_clear();
    char buf[17];
    lcd_set_cursor(0, 0);
    lcd_string("SET WAKTU PROSES");
    lcd_set_cursor(1, 0);
    sprintf(buf, "%d DETIK ", waktuPerlakuan);
    lcd_string(buf);
}

void aturBedaFasa()
{
    lcd_clear();
    char buf[17];
    lcd_set_cursor(0, 0);
    lcd_string("SET BEDA FASA");
    lcd_set_cursor(1, 0);
    if (bedaFasa >= 1000)
        sprintf(buf, "%.1f uS ", bedaFasa / 1000.0);
    else
        sprintf(buf, "%ld nS ", bedaFasa);
    lcd_string(buf);
}

// ===================== FUNGSI LOGIKA BUTTON & MENU =====================
void handle_buttons()
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    selectEvent = 0;
    upEvent = 0;
    downEvent = 0;

    if (!gpio_get(SELECT_BUTTON_PIN))
    {
        if (!select_pressed_flag)
        {
            select_pressed_flag = true;
            last_press_time_select = now;
            selectEvent = 1;
        }
        else if (!select_held_flag && (now - last_press_time_select > HOLD_DELAY_MS))
        {
            select_held_flag = true;
        }
    }
    else
    {
        select_pressed_flag = false;
        select_held_flag = false;
    }

    if (!gpio_get(UP_BUTTON_PIN))
    {
        if (!up_pressed_flag)
        {
            up_pressed_flag = true;
            last_press_time_up = now;
            upEvent = 1;
        }
        else if (now - last_press_time_up > HOLD_DELAY_MS)
        {
            up_held_flag = true;
            if (now - last_repeat_time_up > REPEAT_DELAY_MS)
            {
                upEvent = 2;
                last_repeat_time_up = now;
            }
        }
    }
    else
    {
        up_pressed_flag = false;
        up_held_flag = false;
    }

    if (!gpio_get(DOWN_BUTTON_PIN))
    {
        if (!down_pressed_flag)
        {
            down_pressed_flag = true;
            last_press_time_down = now;
            downEvent = 1;
        }
        else if (now - last_press_time_down > HOLD_DELAY_MS)
        {
            down_held_flag = true;
            if (now - last_repeat_time_down > REPEAT_DELAY_MS)
            {
                downEvent = 2;
                last_repeat_time_down = now;
            }
        }
    }
    else
    {
        down_pressed_flag = false;
        down_held_flag = false;
    }
}

void handle_menu()
{
    if (!subMenu)
    {
        if (upEvent == 1)
        {
            menu++;
            if (menu > 6)
                menu = 1;
            updateMenu();
        }
        if (downEvent == 1)
        {
            menu--;
            if (menu < 1)
                menu = 6;
            updateMenu();
        }
        if (selectEvent == 1)
        {
            if (menu >= 1 && menu <= 4)
            {
                subMenu = true;
                if (menu == 1)
                    aturFrekuensi();
                if (menu == 2)
                    aturLebarPulsa();
                if (menu == 3)
                    aturWaktuPerlakuan();
                if (menu == 4)
                    aturBedaFasa();
            }
            else if (menu == 5) // Mulai Proses - TITIK INTEGRASI KRITIS
            {
                startPulseGeneration();
            }
            else if (menu == 6)
            {
                lcd_clear();
                lcd_set_cursor(0, 0);
                lcd_string("MENGOSONGKAN...");
                sleep_ms(2000);
                updateMenu();
            }
        }
    }
    else
    {
        if (selectEvent == 1)
        {
            save_parameters();
            subMenu = false;
            updateMenu();
        }
        if (menu == 1)
        {
            if (upEvent == 1 || upEvent == 2)
            {
                if (frekuensi < 1000)
                    frekuensi += 10;
                aturFrekuensi();
            }
            if (downEvent == 1 || downEvent == 2)
            {
                if (frekuensi > 10)
                    frekuensi -= 10;
                aturFrekuensi();
            }
        }
        else if (menu == 2)
        {
            if (upEvent == 1 || upEvent == 2)
            {
                if (lebarPulsa < 50000)
                    lebarPulsa += 100;
                aturLebarPulsa();
            }
            if (downEvent == 1 || downEvent == 2)
            {
                if (lebarPulsa > 100)
                    lebarPulsa -= 100;
                aturLebarPulsa();
            }
        }
        else if (menu == 3)
        {
            if (upEvent == 1 || upEvent == 2)
            {
                if (waktuPerlakuan < 30) // Rentang 1-30 detik
                    waktuPerlakuan++;
                aturWaktuPerlakuan();
            }
            if (downEvent == 1 || downEvent == 2)
            {
                if (waktuPerlakuan > 1)
                    waktuPerlakuan--;
                aturWaktuPerlakuan();
            }
        }
        else if (menu == 4)
        {
            if (upEvent == 1 || upEvent == 2)
            {
                if (bedaFasa < 10000)
                    bedaFasa += 100;
                aturBedaFasa();
            }
            if (downEvent == 1 || downEvent == 2)
            {
                if (bedaFasa > 100)
                    bedaFasa -= 100;
                aturBedaFasa();
            }
        }
    }
}

// ===================== FUNGSI PIO DAN GENERASI SINYAL =====================
void init_pio_system()
{
    // Muat program PIO dan klaim state machine
    offset = pio_add_program(pio, &signal_generator_program);
    sm = pio_claim_unused_sm(pio, true);

    // Konfigurasi awal (akan dikonfigurasi ulang setiap kali dimulai)
    pio_sm_config c = signal_generator_program_get_default_config(offset);

    // Konfigurasi pin output
    sm_config_set_set_pins(&c, PIN_CH1_BASE, 4);
    for (uint i = 0; i < 4; ++i)
    {
        pio_gpio_init(pio, PIN_CH1_BASE + i);
    }
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_CH1_BASE, 4, true);

    // Inisialisasi state machine (masih disabled)
    pio_sm_init(pio, sm, offset, &c);
}

void configure_pio_parameters(float freq_hz, float pulse_width_ns, float phase_shift_ns)
{
    // LOGIKA GEM: Konversi parameter UI ke konfigurasi PIO

    // Hitung clock divider yang optimal untuk resolusi yang diinginkan
    float sys_clk_hz = clock_get_hz(clk_sys);
    float target_resolution_ns = 100.0f; // Resolusi 100ns untuk presisi yang baik
    float pio_clk_div = (sys_clk_hz * target_resolution_ns) / 1e9f;

    // Batasi clock divider dalam rentang yang valid (1.0 - 65536.0)
    if (pio_clk_div < 1.0f)
        pio_clk_div = 1.0f;
    if (pio_clk_div > 65536.0f)
        pio_clk_div = 65536.0f;

    printf("Konfigurasi PIO: Freq=%.1f Hz, Pulse=%.1f ns, Phase=%.1f ns\n",
           freq_hz, pulse_width_ns, phase_shift_ns);
    printf("System Clock=%.1f Hz, PIO Clock Div=%.2f\n", sys_clk_hz, pio_clk_div);

    // Rekonfigurasi state machine dengan parameter baru
    pio_sm_config c = signal_generator_program_get_default_config(offset);
    sm_config_set_set_pins(&c, PIN_CH1_BASE, 4);
    sm_config_set_clkdiv(&c, pio_clk_div); // KONEKSI PARAMETER KRITIS

    // Terapkan konfigurasi baru
    pio_sm_init(pio, sm, offset, &c);
}

void calculate_delays(float sys_clk_hz, float pio_clk_div,
                      uint32_t *delay_A, uint32_t *delay_B,
                      uint32_t *delay_C, uint32_t *delay_D,
                      float freq_hz, float pulse_width_ns, float phase_shift_ns)
{
    float pio_clk_hz = sys_clk_hz / pio_clk_div;
    float period_s = 1.0f / freq_hz;
    uint32_t total_pio_cycles = (uint32_t)(period_s * pio_clk_hz);
    uint32_t pulse_width_cycles = (uint32_t)(pulse_width_ns * 1e-9f * pio_clk_hz);
    uint32_t phase_shift_cycles = (uint32_t)(phase_shift_ns * 1e-9f * pio_clk_hz);

    // Durasi setiap event dalam siklus PIO (sesuai sekuens 1001, 0000, 0110, 0000)
    uint32_t event_A_duration = pulse_width_cycles; // CH1/CH4 HIGH
    uint32_t event_B_duration = phase_shift_cycles; // Dead time
    uint32_t event_C_duration = pulse_width_cycles; // CH2/CH3 HIGH
    uint32_t event_D_duration = total_pio_cycles - event_A_duration - event_B_duration - event_C_duration;

    // Nilai N untuk loop counter PIO (dengan overhead 4 siklus per loop)
    *delay_A = event_A_duration > 4 ? event_A_duration - 4 : 0;
    *delay_B = event_B_duration > 4 ? event_B_duration - 4 : 0;
    *delay_C = event_C_duration > 4 ? event_C_duration - 4 : 0;
    *delay_D = event_D_duration > 4 ? event_D_duration - 4 : 0;

    printf("Delays: A=%lu, B=%lu, C=%lu, D=%lu cycles\n", *delay_A, *delay_B, *delay_C, *delay_D);
}

void startPulseGeneration()
{
    // LOGIKA GEM: Baca parameter dari UI dan konfigurasi PIO
    float freq_hz = (float)frekuensi;
    float pulse_width_ns = (float)lebarPulsa;
    float phase_shift_ns = (float)bedaFasa - (float)lebarPulsa;

    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_string("PROSES DIMULAI");

    // Konfigurasi ulang PIO dengan parameter terkini
    configure_pio_parameters(freq_hz, pulse_width_ns, phase_shift_ns);

    // Hitung delay values
    uint32_t delay_A, delay_B, delay_C, delay_D;
    float sys_clk_hz = clock_get_hz(clk_sys);
    float pio_clk_div = (sys_clk_hz * 100.0f) / 1e9f; // Sesuaikan dengan yang digunakan di configure_pio_parameters
    if (pio_clk_div < 1.0f)
        pio_clk_div = 1.0f;
    if (pio_clk_div > 65536.0f)
        pio_clk_div = 65536.0f;

    calculate_delays(sys_clk_hz, pio_clk_div, &delay_A, &delay_B, &delay_C, &delay_D,
                     freq_hz, pulse_width_ns, phase_shift_ns);

    // Set alarm untuk menghentikan proses
    process_complete = false;
    add_alarm_in_ms(waktuPerlakuan * 1000, alarm_callback, NULL, false);

    // Mulai state machine PIO
    pio_sm_set_enabled(pio, sm, true);

    // Loop non-blocking untuk memberikan data ke PIO
    while (!process_complete)
    {
        // Kirim delay values ke PIO secara berulang
        if (!pio_sm_is_tx_fifo_full(pio, sm))
        {
            pio_sm_put(pio, sm, delay_A);
        }
        if (!pio_sm_is_tx_fifo_full(pio, sm))
        {
            pio_sm_put(pio, sm, delay_B);
        }
        if (!pio_sm_is_tx_fifo_full(pio, sm))
        {
            pio_sm_put(pio, sm, delay_C);
        }
        if (!pio_sm_is_tx_fifo_full(pio, sm))
        {
            pio_sm_put(pio, sm, delay_D);
        }

        // Biarkan sistem responsif
        tight_loop_contents();
    }

    // Tampilkan hasil
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_string("PROSES SELESAI!");
    sleep_ms(2000);
    updateMenu();
}

void stopPulseGeneration()
{
    pio_sm_set_enabled(pio, sm, false);
    printf("Generasi pulsa berhenti.\n");
}

// ===================== FUNGSI PENYIMPANAN FLASH =====================
void load_parameters()
{
    const ConfigData *config = (const ConfigData *)(XIP_BASE + FLASH_TARGET_OFFSET);
    if (config->magic == CONFIG_MAGIC)
    {
        printf("Memuat parameter dari flash.\n");
        frekuensi = config->frekuensi;
        lebarPulsa = config->lebarPulsa;
        waktuPerlakuan = config->waktuPerlakuan;
        bedaFasa = config->bedaFasa;
    }
    else
    {
        printf("Magic number tidak valid. Menggunakan nilai default.\n");
    }
}

void save_parameters()
{
    printf("Menyimpan parameter ke flash...\n");
    ConfigData config;
    config.magic = CONFIG_MAGIC;
    config.frekuensi = frekuensi;
    config.lebarPulsa = lebarPulsa;
    config.waktuPerlakuan = waktuPerlakuan;
    config.bedaFasa = bedaFasa;

    uint8_t buffer[FLASH_SECTOR_SIZE];
    memcpy(buffer, &config, sizeof(ConfigData));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    printf("Parameter berhasil disimpan.\n");
}

// ===================== FUNGSI UTAMA =====================
int main()
{
    stdio_init_all();
    sleep_ms(1000);

    // Inisialisasi I2C untuk LCD
    i2c_init(i2c_port, 100 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    lcd_init(i2c_port, LCD_ADDRESS);

    // Inisialisasi tombol
    gpio_init(SELECT_BUTTON_PIN);
    gpio_set_dir(SELECT_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(SELECT_BUTTON_PIN);
    gpio_init(UP_BUTTON_PIN);
    gpio_set_dir(UP_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(UP_BUTTON_PIN);
    gpio_init(DOWN_BUTTON_PIN);
    gpio_set_dir(DOWN_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(DOWN_BUTTON_PIN);

    // Inisialisasi sistem PIO
    init_pio_system();

    // Muat parameter dari flash dan tampilkan menu
    load_parameters();
    updateMenu();

    // Loop utama - tetap responsif
    while (true)
    {
        handle_buttons();
        handle_menu();

        // Proses callback alarm jika ada
        if (process_complete)
        {
            // Flag sudah di-handle di startPulseGeneration()
            // Reset untuk siklus berikutnya
            process_complete = false;
        }

        sleep_ms(DEBOUNCE_DELAY_MS);
    }

    return 0;
}