/**
 * @file xpt2046.c
 */

/* ============== INCLUDES ===================== */

#include "xpt2046.h"
#include "st7789.h"
#include <string.h>
#include <stdio.h>

/* ============== PRIVATE DEFINES ============== */

// XPT2046 Commands (note: X and Y are swapped in datasheet)
#define CMD_X_READ      0x90  // Read X (actually Y in datasheet)
#define CMD_Y_READ      0xD0  // Read Y (actually X in datasheet)
#define CMD_Z1_READ     0xB0  // Read Z1 (pressure)
#define CMD_Z2_READ     0xC0  // Read Z2 (pressure)

// GPIO Macros
#define CS_LOW()   HAL_GPIO_WritePin(XPT2046_CS_PORT, XPT2046_CS_PIN, GPIO_PIN_RESET)
#define CS_HIGH()  HAL_GPIO_WritePin(XPT2046_CS_PORT, XPT2046_CS_PIN, GPIO_PIN_SET)

#ifdef XPT2046_IRQ_PIN
#define IS_IRQ_LOW()  (HAL_GPIO_ReadPin(XPT2046_IRQ_PORT, XPT2046_IRQ_PIN) == GPIO_PIN_RESET)
#endif

#define XPT2046_JUMP_THRESHOLD  80
#define XPT2046_MAX_INVALID_SAMPLES 3

/* ============== PRIVATE VARIABLES ============== */

// Averaging buffer
static int16_t avg_buf_x[XPT2046_AVG_SAMPLES];
static int16_t avg_buf_y[XPT2046_AVG_SAMPLES];
static uint8_t avg_count = 0;

// Calibration values (can be updated at runtime)
static struct {
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
} calibration = {
    XPT2046_X_MIN,
    XPT2046_Y_MIN,
    XPT2046_X_MAX,
    XPT2046_Y_MAX
};

// Screen dimensions (should match ST7789)
static uint16_t screen_width = ST7789_WIDTH;
static uint16_t screen_height = ST7789_HEIGHT;

static int16_t last_valid_x = -1;
static int16_t last_valid_y = -1;
static uint8_t invalid_count = 0;

/* ============== PRIVATE FUNCTIONS ============== */

/**
 * @brief Send command and read 16-bit response
 */
static int16_t XPT2046_SendCommand(uint8_t cmd)
{
    uint8_t tx_data = cmd;
    uint8_t rx_data[2] = {0};

    CS_LOW();
    HAL_Delay(1);

    HAL_SPI_Transmit(&XPT2046_SPI_PORT, &tx_data, 1, HAL_MAX_DELAY);
    for(volatile int i=0; i<100; i++);
    HAL_SPI_Receive(&XPT2046_SPI_PORT, rx_data, 2, HAL_MAX_DELAY);

    CS_HIGH();
    HAL_Delay(1);

    return (rx_data[0] << 8) | rx_data[1];
}

/**
 * @brief Check if touch is detected using pressure reading
 */
static bool XPT2046_IsPressed(void)
{
#ifdef XPT2046_IRQ_PIN
    if (!IS_IRQ_LOW()) return false;
#endif

    int16_t z1 = XPT2046_SendCommand(CMD_Z1_READ) >> 3;
    int16_t z2 = XPT2046_SendCommand(CMD_Z2_READ) >> 3;

    if (z1 < 50) return false;

    int16_t z = z2 - z1;
    return (z > XPT2046_TOUCH_THRESHOLD);
}

/**
 * @brief Apply calibration and coordinate transformations
 */
static void XPT2046_ApplyCalibration(int16_t *x, int16_t *y)
{
#if XPT2046_XY_SWAP != 0
    int16_t temp = *x;
    *x = *y;
    *y = temp;
#endif

    if (*x < calibration.x_min) *x = calibration.x_min;
    if (*y < calibration.y_min) *y = calibration.y_min;
    if (*x > calibration.x_max) *x = calibration.x_max;
    if (*y > calibration.y_max) *y = calibration.y_max;

    *x -= calibration.x_min;
    *y -= calibration.y_min;

    *x = (uint32_t)(*x) * screen_width / (calibration.x_max - calibration.x_min);
    *y = (uint32_t)(*y) * screen_height / (calibration.y_max - calibration.y_min);

#if XPT2046_X_INV != 0
    *x = screen_width - 1 - *x;
#endif

#if XPT2046_Y_INV != 0
    *y = screen_height - 1 - *y;
#endif

    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x >= (int16_t)screen_width) *x = screen_width - 1;
    if (*y >= (int16_t)screen_height) *y = screen_height - 1;
}

/**
 * @brief Average multiple samples to reduce noise
 */
static void XPT2046_Average(int16_t *x, int16_t *y)
{
    for (uint8_t i = XPT2046_AVG_SAMPLES - 1; i > 0; i--)
    {
        avg_buf_x[i] = avg_buf_x[i - 1];
        avg_buf_y[i] = avg_buf_y[i - 1];
    }

    avg_buf_x[0] = *x;
    avg_buf_y[0] = *y;

    if (avg_count < XPT2046_AVG_SAMPLES)
    {
        avg_count++;
    }

    int32_t sum_x = 0;
    int32_t sum_y = 0;

    for (uint8_t i = 0; i < avg_count; i++)
    {
        sum_x += avg_buf_x[i];
        sum_y += avg_buf_y[i];
    }

    *x = sum_x / avg_count;
    *y = sum_y / avg_count;
}

/**
 * @brief Median filter
 */
static int16_t median_filter(int16_t *data, uint8_t size)
{
    int16_t temp[XPT2046_READ_SAMPLES];
    for (uint8_t i = 0; i < size; i++)
    {
        temp[i] = data[i];
    }

    // Bubble sort in copy
    for (uint8_t i = 0; i < size - 1; i++)
    {
        for (uint8_t j = 0; j < size - i - 1; j++)
        {
            if (temp[j] > temp[j + 1])
            {
                int16_t swap = temp[j];
                temp[j] = temp[j + 1];
                temp[j + 1] = swap;
            }
        }
    }

    return temp[size / 2];
}

/**
 * @brief Read coordinates with median filter and outlier removal
 */
static bool XPT2046_ReadFiltered(int16_t *x, int16_t *y)
{
    int16_t x_samples[XPT2046_READ_SAMPLES];
    int16_t y_samples[XPT2046_READ_SAMPLES];

    // Read multiple samples with small delay
    for (uint8_t i = 0; i < XPT2046_READ_SAMPLES; i++)
    {
        x_samples[i] = XPT2046_SendCommand(CMD_X_READ) >> 3;
        y_samples[i] = XPT2046_SendCommand(CMD_Y_READ) >> 3;
        HAL_Delay(2);  // Increased delay from 1ms to 2ms
    }

    // Get median
    *x = median_filter(x_samples, XPT2046_READ_SAMPLES);
    *y = median_filter(y_samples, XPT2046_READ_SAMPLES);

    // Calculate standard deviation to detect noise
    int32_t sum_diff_x = 0, sum_diff_y = 0;
    for (uint8_t i = 0; i < XPT2046_READ_SAMPLES; i++)
    {
        int16_t diff_x = x_samples[i] - *x;
        int16_t diff_y = y_samples[i] - *y;
        sum_diff_x += (diff_x * diff_x);
        sum_diff_y += (diff_y * diff_y);
    }

    int16_t std_dev_x = sum_diff_x / XPT2046_READ_SAMPLES;
    int16_t std_dev_y = sum_diff_y / XPT2046_READ_SAMPLES;

    // If standard deviation is too high, data is unreliable
    if (std_dev_x > 10000 || std_dev_y > 10000)
    {
        return false;  // Unreliable data
    }

    return true;
}

/* ============== PUBLIC FUNCTIONS ============== */

/**
 * @brief Initialize XPT2046
 */
void XPT2046_Init(void)
{
    memset(avg_buf_x, 0, sizeof(avg_buf_x));
    memset(avg_buf_y, 0, sizeof(avg_buf_y));
    avg_count = 0;
    last_valid_x = -1;
    last_valid_y = -1;
    invalid_count = 0;

    // CS pin should be initialized in CubeMX
    CS_HIGH();

    // Small delay for chip to stabilize
    HAL_Delay(10);
}

/**
 * @brief Read calibrated touch coordinates
 */
bool XPT2046_Read(int16_t *x, int16_t *y)
{
	// Check if touch is pressed
    if (!XPT2046_IsPressed())
    {
    	// Reset state when not touched
        avg_count = 0;
        memset(avg_buf_x, 0, sizeof(avg_buf_x));
        memset(avg_buf_y, 0, sizeof(avg_buf_y));
        last_valid_x = -1;
        last_valid_y = -1;
        invalid_count = 0;
        return false;
    }

    // Read with median filter
    int16_t raw_x, raw_y;
    if (!XPT2046_ReadFiltered(&raw_x, &raw_y))
    {
    	// Unreliable data (high noise)
        invalid_count++;
        if (invalid_count >= XPT2046_MAX_INVALID_SAMPLES)
        {
        	// Too many invalid reads, reset
            avg_count = 0;
            last_valid_x = -1;
            last_valid_y = -1;
        }
        return false;
    }

    // Apply calibration
    XPT2046_ApplyCalibration(&raw_x, &raw_y);

    // Check for jump (abnormal point shift)
    if (last_valid_x >= 0 && last_valid_y >= 0)
    {
        int32_t dx = raw_x - last_valid_x;
        int32_t dy = raw_y - last_valid_y;
        int32_t distance_sq = dx * dx + dy * dy;
        int32_t threshold_sq = XPT2046_JUMP_THRESHOLD * XPT2046_JUMP_THRESHOLD;

        if (distance_sq > threshold_sq)
        {
        	// Jump detected
            invalid_count++;

            if (invalid_count >= XPT2046_MAX_INVALID_SAMPLES)
            {
            	// Too many consecutive jumps, assume new touch
                avg_count = 0;
                memset(avg_buf_x, 0, sizeof(avg_buf_x));
                memset(avg_buf_y, 0, sizeof(avg_buf_y));
                invalid_count = 0;
            }
            else
            {
            	// Skip this sample, wait for next
                return false;
            }
        }
        else
        {
        	// Valid sample
            invalid_count = 0;
        }
    }

    // Apply averaging for smoothing
    XPT2046_Average(&raw_x, &raw_y);

    // Store valid coordinates
    last_valid_x = raw_x;
    last_valid_y = raw_y;

    // Update output
    *x = raw_x;
    *y = raw_y;

    return true;
}

/**
 * @brief Check if touch is pressed
 */
bool XPT2046_IsTouched(void)
{
    return XPT2046_IsPressed();
}

/**
 * @brief Read raw uncalibrated coordinates
 */
bool XPT2046_ReadRaw(int16_t *x, int16_t *y)
{
    if (!XPT2046_IsPressed())
    {
        return false;
    }

    // Read values multiple times
    int16_t raw_x = 0;
    int16_t raw_y = 0;

    for (uint8_t i = 0; i < 3; i++)
    {
        raw_x += XPT2046_SendCommand(CMD_X_READ) >> 3;
        raw_y += XPT2046_SendCommand(CMD_Y_READ) >> 3;
    }

    *x = raw_x / 3;
    *y = raw_y / 3;

    return true;
}

/**
 * @brief Update calibration values
 */
void XPT2046_Calibrate(int16_t x_min, int16_t y_min, int16_t x_max, int16_t y_max)
{
    calibration.x_min = x_min;
    calibration.y_min = y_min;
    calibration.x_max = x_max;
    calibration.y_max = y_max;
}

/**
 * @brief Set screen dimensions
 */
void XPT2046_SetScreenSize(uint16_t width, uint16_t height)
{
    screen_width = width;
    screen_height = height;
}

void XPT2046_Test(void)
{
    char buffer[50];
    int16_t x, y;
    int16_t raw_x, raw_y;
    bool was_touching = false;

    // Clear screen and draw border
    ST7789_FillScreen(ST7789_BLACK);
    ST7789_DrawRect(0, 0, ST7789_WIDTH, ST7789_HEIGHT, ST7789_WHITE);

    // Title
    ST7789_WriteString(10, 10, "Touch Test", Font_11x18, ST7789_YELLOW, ST7789_BLACK);
    ST7789_WriteString(10, 35, "Touch to draw dots", Font_7x10, ST7789_CYAN, ST7789_BLACK);

    // Draw corner markers with labels
    ST7789_FillCircle(5, 5, 3, ST7789_RED);
    ST7789_WriteString(10, 5, "(0,0)", Font_7x10, ST7789_RED, ST7789_BLACK);

    ST7789_FillCircle(ST7789_WIDTH - 5, 5, 3, ST7789_RED);
    snprintf(buffer, sizeof(buffer), "(%d,0)", ST7789_WIDTH - 1);
    ST7789_WriteString(ST7789_WIDTH - 50, 5, buffer, Font_7x10, ST7789_RED, ST7789_BLACK);

    ST7789_FillCircle(5, ST7789_HEIGHT - 5, 3, ST7789_RED);
    snprintf(buffer, sizeof(buffer), "(0,%d)", ST7789_HEIGHT - 1);
    ST7789_WriteString(10, ST7789_HEIGHT - 15, buffer, Font_7x10, ST7789_RED, ST7789_BLACK);

    ST7789_FillCircle(ST7789_WIDTH - 5, ST7789_HEIGHT - 5, 3, ST7789_RED);
    snprintf(buffer, sizeof(buffer), "(%d,%d)", ST7789_WIDTH - 1, ST7789_HEIGHT - 1);
    ST7789_WriteString(ST7789_WIDTH - 65, ST7789_HEIGHT - 15, buffer, Font_7x10, ST7789_RED, ST7789_BLACK);

    // Calibration info
    snprintf(buffer, sizeof(buffer), "Cal: %d-%d, %d-%d",
             XPT2046_X_MIN, XPT2046_X_MAX, XPT2046_Y_MIN, XPT2046_Y_MAX);
    ST7789_WriteString(10, 55, buffer, Font_7x10, ST7789_WHITE, ST7789_BLACK);

    while (1)
    {
        bool is_touching = XPT2046_Read(&x, &y);

        if (is_touching)
        {
            // Draw dot at touch position
            ST7789_FillCircle(x, y, 3, ST7789_GREEN);
            ST7789_DrawCircle(x, y, 5, ST7789_WHITE);

            // Update coordinate display (clear old text first)
            if (!was_touching)
            {
                ST7789_FillRect(0, 75, ST7789_WIDTH, 50, ST7789_BLACK);
            }

            // Show screen coordinates
            snprintf(buffer, sizeof(buffer), "Screen: (%3d,%3d)", x, y);
            ST7789_WriteString(10, 80, buffer, Font_11x18, ST7789_GREEN, ST7789_BLACK);

            // Show raw coordinates
            if (XPT2046_ReadRaw(&raw_x, &raw_y))
            {
                snprintf(buffer, sizeof(buffer), "Raw: (%4d,%4d)", raw_x, raw_y);
                ST7789_WriteString(10, 105, buffer, Font_7x10, ST7789_CYAN, ST7789_BLACK);
            }

            was_touching = true;
        }
        else
        {
            // Touch released
            if (was_touching)
            {
                // Clear coordinate display
                ST7789_FillRect(0, 75, ST7789_WIDTH, 50, ST7789_BLACK);
                ST7789_WriteString(10, 90, "Released", Font_11x18, ST7789_GRAY, ST7789_BLACK);
                was_touching = false;
            }
        }

        HAL_Delay(30);  // ~30fps update rate
    }
}

void XPT2046_HardwareTest(void)
{
    char buffer[100];
    uint16_t y_pos = 10;

    ST7789_FillScreen(ST7789_BLACK);
    ST7789_WriteString(10, y_pos, "XPT2046 Hardware Test", Font_11x18, ST7789_YELLOW, ST7789_BLACK);
    y_pos += 30;

    // Test 1: Check CS Pin
    ST7789_WriteString(10, y_pos, "Test 1: CS Pin", Font_7x10, ST7789_CYAN, ST7789_BLACK);
    y_pos += 15;

    CS_HIGH();
    HAL_Delay(5);
    GPIO_PinState cs_state = HAL_GPIO_ReadPin(XPT2046_CS_PORT, XPT2046_CS_PIN);

    snprintf(buffer, sizeof(buffer), "CS: %s", cs_state == GPIO_PIN_SET ? "HIGH (OK)" : "LOW (BAD)");
    ST7789_WriteString(10, y_pos, buffer, Font_7x10,
                      cs_state == GPIO_PIN_SET ? ST7789_GREEN : ST7789_RED, ST7789_BLACK);
    y_pos += 20;

    // Test 2: Read X (multiple samples)
    ST7789_WriteString(10, y_pos, "Test 2: Read X", Font_7x10, ST7789_CYAN, ST7789_BLACK);
    y_pos += 15;

    int32_t x_sum = 0;
    for (uint8_t i = 0; i < 5; i++)
    {
        CS_LOW();
        HAL_Delay(1);

        uint8_t cmd = CMD_X_READ;
        uint8_t rx[2] = {0};

        HAL_SPI_Transmit(&XPT2046_SPI_PORT, &cmd, 1, 100);
        HAL_Delay(1);  // Wait for conversion
        HAL_SPI_Receive(&XPT2046_SPI_PORT, rx, 2, 100);

        CS_HIGH();
        HAL_Delay(2);

        int16_t x_raw = ((rx[0] << 8) | rx[1]) >> 3;
        x_sum += x_raw;
    }

    int16_t x_avg = x_sum / 5;
    snprintf(buffer, sizeof(buffer), "X avg: %d (0x%03X)", x_avg, x_avg);
    uint16_t color = (x_avg > 100 && x_avg < 4000) ? ST7789_GREEN : ST7789_ORANGE;
    ST7789_WriteString(10, y_pos, buffer, Font_7x10, color, ST7789_BLACK);
    y_pos += 20;

    // Test 3: Read Y (multiple samples)
    ST7789_WriteString(10, y_pos, "Test 3: Read Y", Font_7x10, ST7789_CYAN, ST7789_BLACK);
    y_pos += 15;

    int32_t y_sum = 0;
    for (uint8_t i = 0; i < 5; i++)
    {
        CS_LOW();
        HAL_Delay(1);

        uint8_t cmd = CMD_Y_READ;
        uint8_t rx[2] = {0};

        HAL_SPI_Transmit(&XPT2046_SPI_PORT, &cmd, 1, 100);
        HAL_Delay(1);
        HAL_SPI_Receive(&XPT2046_SPI_PORT, rx, 2, 100);

        CS_HIGH();
        HAL_Delay(2);

        int16_t y_raw = ((rx[0] << 8) | rx[1]) >> 3;
        y_sum += y_raw;
    }

    int16_t y_avg = y_sum / 5;
    snprintf(buffer, sizeof(buffer), "Y avg: %d (0x%03X)", y_avg, y_avg);
    color = (y_avg > 100 && y_avg < 4000) ? ST7789_GREEN : ST7789_ORANGE;
    ST7789_WriteString(10, y_pos, buffer, Font_7x10, color, ST7789_BLACK);
    y_pos += 20;

    // Test 4: Read Z1/Z2 (pressure)
    ST7789_WriteString(10, y_pos, "Test 4: Pressure", Font_7x10, ST7789_CYAN, ST7789_BLACK);
    y_pos += 15;

    // Z1
    CS_LOW();
    HAL_Delay(1);
    uint8_t cmd_z1 = CMD_Z1_READ;
    uint8_t rx_z1[2] = {0};
    HAL_SPI_Transmit(&XPT2046_SPI_PORT, &cmd_z1, 1, 100);
    HAL_Delay(1);
    HAL_SPI_Receive(&XPT2046_SPI_PORT, rx_z1, 2, 100);
    CS_HIGH();
    HAL_Delay(2);

    int16_t z1 = ((rx_z1[0] << 8) | rx_z1[1]) >> 3;

    // Z2
    CS_LOW();
    HAL_Delay(1);
    uint8_t cmd_z2 = CMD_Z2_READ;
    uint8_t rx_z2[2] = {0};
    HAL_SPI_Transmit(&XPT2046_SPI_PORT, &cmd_z2, 1, 100);
    HAL_Delay(1);
    HAL_SPI_Receive(&XPT2046_SPI_PORT, rx_z2, 2, 100);
    CS_HIGH();
    HAL_Delay(2);

    int16_t z2 = ((rx_z2[0] << 8) | rx_z2[1]) >> 3;
    int16_t pressure = z2 - z1;

    snprintf(buffer, sizeof(buffer), "Z1=%d Z2=%d P=%d", z1, z2, pressure);
    ST7789_WriteString(10, y_pos, buffer, Font_7x10, ST7789_WHITE, ST7789_BLACK);
    y_pos += 15;

    snprintf(buffer, sizeof(buffer), "Threshold: %d", XPT2046_TOUCH_THRESHOLD);
    ST7789_WriteString(10, y_pos, buffer, Font_7x10, ST7789_YELLOW, ST7789_BLACK);
    y_pos += 20;

    // Test 5: SPI Speed
    ST7789_WriteString(10, y_pos, "Test 5: SPI Config", Font_7x10, ST7789_CYAN, ST7789_BLACK);
    y_pos += 15;

    uint32_t pclk = HAL_RCC_GetPCLK2Freq();
    uint32_t prescaler = (1 << ((XPT2046_SPI_PORT.Init.BaudRatePrescaler >> 3) + 1));
    uint32_t baudrate = pclk / prescaler;

    snprintf(buffer, sizeof(buffer), "SPI: %lu Hz", baudrate);
    color = (baudrate <= 2000000) ? ST7789_GREEN : ST7789_RED;
    ST7789_WriteString(10, y_pos, buffer, Font_7x10, color, ST7789_BLACK);
    y_pos += 15;

    if (baudrate > 2000000)
    {
        ST7789_WriteString(10, y_pos, "WARNING: Too fast!", Font_7x10, ST7789_RED, ST7789_BLACK);
        y_pos += 15;
        ST7789_WriteString(10, y_pos, "Max: 2 MHz", Font_7x10, ST7789_RED, ST7789_BLACK);
    }
    else
    {
        ST7789_WriteString(10, y_pos, "Speed OK!", Font_7x10, ST7789_GREEN, ST7789_BLACK);
    }

    HAL_Delay(5000);
}

void XPT2046_LiveTest(void)
{
    char buffer[100];
    uint32_t last_update = 0;

    ST7789_FillScreen(ST7789_BLACK);
    ST7789_WriteString(10, 10, "XPT2046 Live Test", Font_11x18, ST7789_YELLOW, ST7789_BLACK);
    ST7789_WriteString(10, 35, "Touch to see values", Font_7x10, ST7789_WHITE, ST7789_BLACK);

    // Draw threshold line
    snprintf(buffer, sizeof(buffer), "Threshold: %d", XPT2046_TOUCH_THRESHOLD);
    ST7789_WriteString(10, 50, buffer, Font_7x10, ST7789_YELLOW, ST7789_BLACK);

    while (1)
    {
        uint32_t now = HAL_GetTick();

        // Update every 100ms
        if (now - last_update < 100)
        {
            HAL_Delay(10);
            continue;
        }
        last_update = now;

        // Clear display area
        ST7789_FillRect(0, 70, ST7789_WIDTH, 140, ST7789_BLACK);

        // Read X with proper timing
        CS_LOW();
        HAL_Delay(1);
        uint8_t cmd = CMD_X_READ;
        uint8_t rx[2];
        HAL_SPI_Transmit(&XPT2046_SPI_PORT, &cmd, 1, 100);
        HAL_Delay(1);
        HAL_SPI_Receive(&XPT2046_SPI_PORT, rx, 2, 100);
        CS_HIGH();
        HAL_Delay(1);
        int16_t x = ((rx[0] << 8) | rx[1]) >> 3;

        // Read Y
        CS_LOW();
        HAL_Delay(1);
        cmd = CMD_Y_READ;
        HAL_SPI_Transmit(&XPT2046_SPI_PORT, &cmd, 1, 100);
        HAL_Delay(1);
        HAL_SPI_Receive(&XPT2046_SPI_PORT, rx, 2, 100);
        CS_HIGH();
        HAL_Delay(1);
        int16_t y = ((rx[0] << 8) | rx[1]) >> 3;

        // Read Z1
        CS_LOW();
        HAL_Delay(1);
        cmd = CMD_Z1_READ;
        HAL_SPI_Transmit(&XPT2046_SPI_PORT, &cmd, 1, 100);
        HAL_Delay(1);
        HAL_SPI_Receive(&XPT2046_SPI_PORT, rx, 2, 100);
        CS_HIGH();
        HAL_Delay(1);
        int16_t z1 = ((rx[0] << 8) | rx[1]) >> 3;

        // Read Z2
        CS_LOW();
        HAL_Delay(1);
        cmd = CMD_Z2_READ;
        HAL_SPI_Transmit(&XPT2046_SPI_PORT, &cmd, 1, 100);
        HAL_Delay(1);
        HAL_SPI_Receive(&XPT2046_SPI_PORT, rx, 2, 100);
        CS_HIGH();
        HAL_Delay(1);
        int16_t z2 = ((rx[0] << 8) | rx[1]) >> 3;

        int16_t pressure = z2 - z1;

        // Display values
        snprintf(buffer, sizeof(buffer), "X: %4d", x);
        ST7789_WriteString(10, 80, buffer, Font_11x18, ST7789_GREEN, ST7789_BLACK);

        snprintf(buffer, sizeof(buffer), "Y: %4d", y);
        ST7789_WriteString(10, 105, buffer, Font_11x18, ST7789_GREEN, ST7789_BLACK);

        snprintf(buffer, sizeof(buffer), "Z1:%4d Z2:%4d", z1, z2);
        ST7789_WriteString(10, 130, buffer, Font_7x10, ST7789_CYAN, ST7789_BLACK);

        snprintf(buffer, sizeof(buffer), "Pressure: %4d", pressure);
        uint16_t pcolor = (pressure > XPT2046_TOUCH_THRESHOLD) ? ST7789_RED : ST7789_GRAY;
        ST7789_WriteString(10, 150, buffer, Font_11x18, pcolor, ST7789_BLACK);

        // Touch indicator circle
        if (pressure > XPT2046_TOUCH_THRESHOLD)
        {
            ST7789_FillCircle(290, 100, 15, ST7789_RED);
            ST7789_WriteString(220, 125, "TOUCH!", Font_11x18, ST7789_RED, ST7789_BLACK);
        }
        else
        {
            ST7789_DrawCircle(290, 100, 15, ST7789_GRAY);
            ST7789_WriteString(220, 125, "      ", Font_11x18, ST7789_BLACK, ST7789_BLACK);
        }
    }
}

/**
 * @brief Interactive calibration tool.
 * Guides user to touch 5 points for automatic calibration calculation.
 */
void XPT2046_Calibration(void)
{
    char buffer[100];
    int16_t raw_x, raw_y;

    // Calibration points (screen coordinates)
    struct {
        uint16_t screen_x;
        uint16_t screen_y;
        int16_t raw_x;
        int16_t raw_y;
        bool captured;
    } points[5] = {
        {10, 10, 0, 0, false},								// Top-left
        {ST7789_WIDTH-10, 10, 0, 0, false},					// Top-right
        {ST7789_WIDTH-10, ST7789_HEIGHT-10, 0, 0, false},	// Bottom-right
        {10, ST7789_HEIGHT-10, 0, 0, false},				// Bottom-left
        {ST7789_WIDTH/2, ST7789_HEIGHT/2, 0, 0, false}		// Center
    };

    ST7789_FillScreen(ST7789_BLACK);
    ST7789_WriteString(10, 10, "Calibration", Font_11x18, ST7789_YELLOW, ST7789_BLACK);
    ST7789_WriteString(10, 35, "Touch the RED target", Font_7x10, ST7789_CYAN, ST7789_BLACK);
    ST7789_WriteString(10, 50, "Hold for 1 second", Font_7x10, ST7789_CYAN, ST7789_BLACK);

    HAL_Delay(2000);

    for (uint8_t i = 0; i < 5; i++)
    {
        ST7789_FillScreen(ST7789_BLACK);

        // Draw instruction
        snprintf(buffer, sizeof(buffer), "Point %d/5", i + 1);
        ST7789_WriteString(10, 10, buffer, Font_11x18, ST7789_YELLOW, ST7789_BLACK);

        const char* labels[] = {"Top-Left", "Top-Right", "Bottom-Right", "Bottom-Left", "Center"};
        ST7789_WriteString(10, 35, labels[i], Font_7x10, ST7789_CYAN, ST7789_BLACK);

        // Draw target
        uint16_t tx = points[i].screen_x;
        uint16_t ty = points[i].screen_y;

        ST7789_DrawCircle(tx, ty, 20, ST7789_RED);
        ST7789_DrawCircle(tx, ty, 15, ST7789_RED);
        ST7789_DrawCircle(tx, ty, 10, ST7789_RED);
        ST7789_FillCircle(tx, ty, 5, ST7789_RED);

        // Wait for touch
        bool waiting = true;
        uint32_t touch_start = 0;
        int32_t sum_x = 0, sum_y = 0;
        uint8_t samples = 0;

        while (waiting)
        {
            if (XPT2046_ReadRaw(&raw_x, &raw_y))
            {
                if (touch_start == 0)
                {
                    touch_start = HAL_GetTick();
                    sum_x = 0;
                    sum_y = 0;
                    samples = 0;
                }

                // Accumulate samples
                sum_x += raw_x;
                sum_y += raw_y;
                samples++;

                uint32_t hold_time = HAL_GetTick() - touch_start;

                // Progress indicator
                ST7789_FillRect(0, 60, ST7789_WIDTH, 20, ST7789_BLACK);
                snprintf(buffer, sizeof(buffer), "Hold: %lu ms", hold_time);
                ST7789_WriteString(10, 65, buffer, Font_7x10, ST7789_GREEN, ST7789_BLACK);

                // Progress bar
                uint16_t bar_width = (hold_time * 200) / 1000;
                if (bar_width > 200) bar_width = 200;
                ST7789_FillRect(10, 85, bar_width, 10, ST7789_GREEN);
                ST7789_DrawRect(10, 85, 200, 10, ST7789_WHITE);

                // Captured after 1 second
                if (hold_time >= 1000 && samples > 0)
                {
                    points[i].raw_x = sum_x / samples;
                    points[i].raw_y = sum_y / samples;
                    points[i].captured = true;

                    // Feedback
                    ST7789_FillCircle(tx, ty, 8, ST7789_GREEN);
                    snprintf(buffer, sizeof(buffer), "OK: %d,%d", points[i].raw_x, points[i].raw_y);
                    ST7789_WriteString(10, 105, buffer, Font_7x10, ST7789_GREEN, ST7789_BLACK);
                    HAL_Delay(1000);

                    waiting = false;
                }
            }
            else
            {
                // Touch released, reset
                touch_start = 0;
                sum_x = 0;
                sum_y = 0;
                samples = 0;
            }

            HAL_Delay(50);
        }
    }

    // Calculate calibration values
    int16_t x_min = 4095, x_max = 0;
    int16_t y_min = 4095, y_max = 0;

    // Check all 4 corners only
    for (uint8_t i = 0; i < 4; i++)
    {
        if (points[i].raw_x < x_min) x_min = points[i].raw_x;
        if (points[i].raw_x > x_max) x_max = points[i].raw_x;
        if (points[i].raw_y < y_min) y_min = points[i].raw_y;
        if (points[i].raw_y > y_max) y_max = points[i].raw_y;
    }

    // Display results
    ST7789_FillScreen(ST7789_BLACK);
    ST7789_WriteString(10, 10, "Calibration Results", Font_11x18, ST7789_YELLOW, ST7789_BLACK);

    uint16_t y_pos = 40;

    snprintf(buffer, sizeof(buffer), "X_MIN: %d", x_min);
    ST7789_WriteString(10, y_pos, buffer, Font_11x18, ST7789_GREEN, ST7789_BLACK);
    y_pos += 25;

    snprintf(buffer, sizeof(buffer), "X_MAX: %d", x_max);
    ST7789_WriteString(10, y_pos, buffer, Font_11x18, ST7789_GREEN, ST7789_BLACK);
    y_pos += 25;

    snprintf(buffer, sizeof(buffer), "Y_MIN: %d", y_min);
    ST7789_WriteString(10, y_pos, buffer, Font_11x18, ST7789_GREEN, ST7789_BLACK);
    y_pos += 25;

    snprintf(buffer, sizeof(buffer), "Y_MAX: %d", y_max);
    ST7789_WriteString(10, y_pos, buffer, Font_11x18, ST7789_GREEN, ST7789_BLACK);
    y_pos += 35;

    // Show configuration to update in xpt2046.h
    ST7789_WriteString(10, y_pos, "Update xpt2046.h:", Font_7x10, ST7789_CYAN, ST7789_BLACK);
    y_pos += 15;

    snprintf(buffer, sizeof(buffer), "#define XPT2046_X_MIN %d", x_min);
    ST7789_WriteString(10, y_pos, buffer, Font_7x10, ST7789_WHITE, ST7789_BLACK);
    y_pos += 12;

    snprintf(buffer, sizeof(buffer), "#define XPT2046_X_MAX %d", x_max);
    ST7789_WriteString(10, y_pos, buffer, Font_7x10, ST7789_WHITE, ST7789_BLACK);
    y_pos += 12;

    snprintf(buffer, sizeof(buffer), "#define XPT2046_Y_MIN %d", y_min);
    ST7789_WriteString(10, y_pos, buffer, Font_7x10, ST7789_WHITE, ST7789_BLACK);
    y_pos += 12;

    snprintf(buffer, sizeof(buffer), "#define XPT2046_Y_MAX %d", y_max);
    ST7789_WriteString(10, y_pos, buffer, Font_7x10, ST7789_WHITE, ST7789_BLACK);

    // Apply calibration immediately for testing
    XPT2046_Calibrate(x_min, y_min, x_max, y_max);

    HAL_Delay(5000);

    // Now run test with new calibration
    ST7789_WriteString(10, 220, "Running test...", Font_7x10, ST7789_YELLOW, ST7789_BLACK);
    HAL_Delay(1000);

    XPT2046_Test();
}

/**
 * @brief Quick diagnostic - check raw values at current touch
 */
void XPT2046_RawDiagnostic(void) {
    char buffer[100];
    int16_t raw_x, raw_y;

    ST7789_FillScreen(ST7789_BLACK);
    ST7789_WriteString(10, 10, "Raw Diagnostic", Font_11x18, ST7789_YELLOW, ST7789_BLACK);
    ST7789_WriteString(10, 35, "Touch anywhere", Font_7x10, ST7789_CYAN, ST7789_BLACK);

    // Show current calibration
    snprintf(buffer, sizeof(buffer), "Current Cal:");
    ST7789_WriteString(10, 55, buffer, Font_7x10, ST7789_WHITE, ST7789_BLACK);

    snprintf(buffer, sizeof(buffer), "X: %d - %d", XPT2046_X_MIN, XPT2046_X_MAX);
    ST7789_WriteString(10, 70, buffer, Font_7x10, ST7789_GRAY, ST7789_BLACK);

    snprintf(buffer, sizeof(buffer), "Y: %d - %d", XPT2046_Y_MIN, XPT2046_Y_MAX);
    ST7789_WriteString(10, 85, buffer, Font_7x10, ST7789_GRAY, ST7789_BLACK);

    while (1) {
        if (XPT2046_ReadRaw(&raw_x, &raw_y)) {
            // Clear display area
            ST7789_FillRect(0, 110, ST7789_WIDTH, 100, ST7789_BLACK);

            snprintf(buffer, sizeof(buffer), "Raw X: %d", raw_x);
            ST7789_WriteString(10, 115, buffer, Font_11x18, ST7789_GREEN, ST7789_BLACK);

            snprintf(buffer, sizeof(buffer), "Raw Y: %d", raw_y);
            ST7789_WriteString(10, 140, buffer, Font_11x18, ST7789_GREEN, ST7789_BLACK);

            // Show if in range
            bool x_in_range = (raw_x >= XPT2046_X_MIN && raw_x <= XPT2046_X_MAX);
            bool y_in_range = (raw_y >= XPT2046_Y_MIN && raw_y <= XPT2046_Y_MAX);

            snprintf(buffer, sizeof(buffer), "X: %s", x_in_range ? "IN RANGE" : "OUT OF RANGE");
            ST7789_WriteString(10, 170, buffer, Font_7x10,
                             x_in_range ? ST7789_GREEN : ST7789_RED, ST7789_BLACK);

            snprintf(buffer, sizeof(buffer), "Y: %s", y_in_range ? "IN RANGE" : "OUT OF RANGE");
            ST7789_WriteString(10, 185, buffer, Font_7x10,
                             y_in_range ? ST7789_GREEN : ST7789_RED, ST7789_BLACK);
        }

        HAL_Delay(50);
    }
}
