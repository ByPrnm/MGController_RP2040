/**
 * Integrasi Menu LCD dengan Generator Sinyal PIO.
 *
 * Koneksi Perangkat Keras:
 * - LCD I2C:
 * - SDA -> GP4
 * - SCL -> GP5
 * - Tombol (pull-up internal, ke GND saat ditekan):
 * - PILIH -> GP13
 * - ATAS   -> GP14
 * - BAWAH -> GP15
 * - Output Sinyal PIO:
 * - CH1 -> GP6
 * - CH2 -> GP7
 * - CH3 -> GP8
 * - CH4 -> GP9
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
#include "hardware/dma.h"
#include "lib/lcd_i2c.h"
#include "signal_generator.pio.h" // Header yang di-generate dari file .pio

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

// ===================== KONFIGURASI PIN & PERIFERAL =====================
const uint8_t LCD_ADDRESS = 0x27;
const uint I2C_SDA_PIN = 4;
const uint I2C_SCL_PIN = 5;
i2c_inst_t *i2c_port = i2c0;

const uint SELECT_BUTTON_PIN = 13;
const uint UP_BUTTON_PIN = 14;
const uint DOWN_BUTTON_PIN = 15;

const uint PIN_CH1_BASE = 6; // Pin dasar untuk output sinyal

// ===================== VARIABEL GLOBAL =====================
// Variabel Menu
uint8_t menu = 1;
volatile long frekuensi = 100;
volatile long lebarPulsa = 3500;
volatile int waktuPerlakuan = 3;
volatile long bedaFasa = 100;
bool subMenu = false;
const int DISCHARGE_TIME_S = 5;

// Variabel PIO & DMA
PIO pio_instance = pio0;
uint sm_instance;
uint offset_instance;
int dma_chan;
uint32_t dma_delay_buffer[4]; // Buffer untuk menyimpan 4 nilai delay

// Variabel Debounce
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
void load_parameters();
void save_parameters();
void run_generic_countdown(int duration_s, const char *line1, const char *line2_prefix);

// Prototipe Fungsi Generator Sinyal
void calculate_and_load_delays();
void startPulseGeneration();
void stopPulseGeneration();

// ===================== FUNGSI TAMPILAN LCD (Tidak Berubah) =====================
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

// ===================== FUNGSI LOGIKA (Tidak Berubah) =====================
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
            else if (menu == 5)
            {
                startPulseGeneration();
                run_generic_countdown(waktuPerlakuan, "PROSES BERJALAN", "SISA WAKTU: ");
                stopPulseGeneration();
                updateMenu();
            }
            else if (menu == 6)
            {
                run_generic_countdown(DISCHARGE_TIME_S, "MENGOSONGKAN...", "SISA WAKTU: ");
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
                if (waktuPerlakuan < 60)
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
void run_generic_countdown(int duration_s, const char *line1, const char *line2_prefix)
{
    char buf[17];
    for (int i = duration_s; i > 0; i--)
    {
        lcd_clear();
        lcd_set_cursor(0, 0);
        lcd_string(line1);
        lcd_set_cursor(1, 0);
        sprintf(buf, "%s%2d", line2_prefix, i);
        lcd_string(buf);
        sleep_ms(1000);
    }
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_string("PROSES SELESAI");
    sleep_ms(1500);
}

// ===================== FUNGSI GENERATOR SINYAL (BARU) =====================
void calculate_and_load_delays()
{
    // Tentukan clock divider untuk PIO agar 1 siklus = 0.1 us (10MHz)
    float pio_clk_div = (float)clock_get_hz(clk_sys) / 10000000.0f;
    float pio_freq_hz = (float)clock_get_hz(clk_sys) / pio_clk_div;

    // Konversi parameter dari menu (Hz, ns) ke (Hz, us) untuk kalkulasi
    float freq_hz_f = (float)frekuensi;
    float pulse_width_us_f = (float)lebarPulsa / 1000.0f;
    float phase_shift_us_f = (float)bedaFasa / 1000.0f;

    float period_us = 1e6f / freq_hz_f;
    float duration_D_us = period_us - pulse_width_us_f - phase_shift_us_f - pulse_width_us_f;

    // Pastikan durasi tidak negatif
    if (duration_D_us < 0)
        duration_D_us = 0;

    uint32_t cycles_A = (uint32_t)(pulse_width_us_f * (pio_freq_hz / 1e6f));
    uint32_t cycles_B = (uint32_t)(phase_shift_us_f * (pio_freq_hz / 1e6f));
    uint32_t cycles_C = (uint32_t)(pulse_width_us_f * (pio_freq_hz / 1e6f));
    uint32_t cycles_D = (uint32_t)(duration_D_us * (pio_freq_hz / 1e6f));

    // Koreksi untuk overhead instruksi PIO (pull, mov, set, jmp)
    dma_delay_buffer[0] = cycles_A > 4 ? cycles_A - 4 : 0;
    dma_delay_buffer[1] = cycles_B > 4 ? cycles_B - 4 : 0;
    dma_delay_buffer[2] = cycles_C > 4 ? cycles_C - 4 : 0;
    dma_delay_buffer[3] = cycles_D > 4 ? cycles_D - 4 : 0;
}

void startPulseGeneration()
{
    printf("Mulai generasi pulsa...\n");

    // Muat program PIO ke instruction memory
    offset_instance = pio_add_program(pio_instance, &signal_generator_program);

    // Klaim state machine
    sm_instance = pio_claim_unused_sm(pio_instance, true);

    // Dapatkan konfigurasi default dan modifikasi
    pio_sm_config c = signal_generator_program_get_default_config(offset_instance);
    sm_config_set_set_pins(&c, PIN_CH1_BASE, 4);
    float pio_clk_div = (float)clock_get_hz(clk_sys) / 10000000.0f;
    sm_config_set_clkdiv(&c, pio_clk_div);

    // Inisialisasi GPIO untuk PIO
    for (uint i = 0; i < 4; ++i)
        pio_gpio_init(pio_instance, PIN_CH1_BASE + i);
    pio_sm_set_consecutive_pindirs(pio_instance, sm_instance, PIN_CH1_BASE, 4, true);

    // Muat konfigurasi ke SM
    pio_sm_init(pio_instance, sm_instance, offset_instance, &c);

    // Hitung delay berdasarkan parameter menu dan isi buffer DMA
    calculate_and_load_delays();

    // Konfigurasi DMA untuk mentransfer data ke PIO TX FIFO secara otomatis
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dma_c = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dma_c, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_c, true);
    channel_config_set_write_increment(&dma_c, false);
    channel_config_set_dreq(&dma_c, pio_get_dreq(pio_instance, sm_instance, true));
    channel_config_set_ring(&dma_c, true, 2); // Ring buffer 4 words (2^2)

    dma_channel_configure(
        dma_chan,
        &dma_c,
        &pio_instance->txf[sm_instance], // Write address (PIO TX FIFO)
        dma_delay_buffer,                // Read address (our buffer)
        4,                               // Number of transfers
        true                             // Start immediately
    );

    // Aktifkan state machine PIO
    pio_sm_set_enabled(pio_instance, sm_instance, true);
}

void stopPulseGeneration()
{
    printf("Generasi pulsa berhenti.\n");

    // Hentikan PIO dan DMA
    pio_sm_set_enabled(pio_instance, sm_instance, false);
    dma_channel_abort(dma_chan);

    // Lepaskan sumber daya
    dma_channel_unclaim(dma_chan);
    pio_remove_program(pio_instance, &signal_generator_program, offset_instance);
    pio_sm_unclaim(pio_instance, sm_instance);
}

// ===================== FUNGSI PENYIMPANAN FLASH (Tidak Berubah) =====================
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

    // Inisialisasi I2C & LCD
    i2c_init(i2c_port, 100 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    lcd_init(i2c_port, LCD_ADDRESS);

    // Inisialisasi Tombol
    gpio_init(SELECT_BUTTON_PIN);
    gpio_set_dir(SELECT_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(SELECT_BUTTON_PIN);
    gpio_init(UP_BUTTON_PIN);
    gpio_set_dir(UP_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(UP_BUTTON_PIN);
    gpio_init(DOWN_BUTTON_PIN);
    gpio_set_dir(DOWN_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(DOWN_BUTTON_PIN);

    load_parameters();
    updateMenu();

    while (true)
    {
        handle_buttons();
        handle_menu();
        sleep_ms(DEBOUNCE_DELAY_MS);
    }
    return 0;
}
