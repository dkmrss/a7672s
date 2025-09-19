/* app_main.c — OpenCPU skeleton */
#include "ql_api_osi.h"
#include "ql_api_gpio.h"
#include "ql_api_adc.h"
#include "ql_api_uart.h"
#include "ql_api_sms.h"
#include "ql_api_call.h"
#include "ql_api_debug.h"
#include <string.h>
#include <stdio.h>

#define ADMIN_PHONE      "+84123456789"   // số admin, thay bằng số thật
#define SAMPLE_COUNT     50
#define ADC_MAX          4095.0
#define VREF             3.3
#define SENSOR_CENTER_V  1.65   // center voltage nếu sensor cấp 3.3V
#define SENSOR_SENS_VPERA 0.066 // 66 mV/A ví dụ (thay theo sensor)
#define THRESHOLD_FAULT_A 0.5   // ngưỡng lỗi (A) - chỉnh theo yêu cầu
#define HYSTERESIS_A      0.1

// GPIO pin mapping (cần map đúng theo datasheet / schematic)
#define PIN_OUTPUT1    PINNAME_GPIO1
#define PIN_INPUT1     PINNAME_GPIO2
#define PIN_RESET      PINNAME_GPIO3
#define PIN_EN_CHARGE  PINNAME_GPIO4
#define PIN_PW_DET     PINNAME_GPIO5
#define ADC_PIN        ADC_CH0         // ví dụ

static int is_alerting = 0;

static double read_current_A(void) {
    int i;
    double sum = 0;
    for (i = 0; i < SAMPLE_COUNT; i++) {
        int adc = Ql_ADC_Get(ADC_PIN); // giả sử có API Ql_ADC_Get
        double v = (adc * VREF) / ADC_MAX;
        double cur = (v - SENSOR_CENTER_V) / SENSOR_SENS_VPERA;
        sum += cur;
        Ql_Sleep(10);
    }
    return sum / SAMPLE_COUNT;
}

static void send_sms_text(const char *phone, const char *msg) {
    // dùng API SMS nếu có
    Ql_Sms_SendText(phone, msg); // giả sử API; thay bằng cách gửi AT nếu không có
    Ql_Debug_Trace("SMS sent to %s: %s\r\n", phone, msg);
}

static void do_call(const char *phone) {
    Ql_Call_Make(phone); // giả sử API; thay bằng ATD nếu cần
    Ql_Debug_Trace("Calling %s\r\n", phone);
}

void proc_main_task(void) {
    Ql_GPIO_Init(PIN_OUTPUT1, PINDIRECTION_OUT, PINLEVEL_LOW, PINPULLSEL_DISABLE);
    Ql_GPIO_Init(PIN_INPUT1, PINDIRECTION_IN, PINLEVEL_LOW, PINPULLSEL_PULLDOWN);
    Ql_GPIO_Init(PIN_RESET,  PINDIRECTION_OUT, PINLEVEL_HIGH, PINPULLSEL_DISABLE);
    Ql_GPIO_Init(PIN_EN_CHARGE, PINDIRECTION_OUT, PINLEVEL_LOW, PINPULLSEL_DISABLE);
    Ql_GPIO_Init(PIN_PW_DET, PINDIRECTION_IN, PINLEVEL_LOW, PINPULLSEL_PULLDOWN);

    double last_current = 0;

    while (1) {
        double iA = read_current_A();
        Ql_Debug_Trace("Current = %.3f A\r\n", iA);

        // Phát hiện sự cố: mất dòng đột ngột (dưới threshold nhỏ)
        if (iA < THRESHOLD_FAULT_A && last_current >= THRESHOLD_FAULT_A + HYSTERESIS_A) {
            // Lỗi: line lost
            if (!is_alerting) {
                char msg[160];
                snprintf(msg, sizeof(msg), "Canh bao: Mat dong dot ngot! (%.3fA)", iA);
                send_sms_text(ADMIN_PHONE, msg);
                // Nếu nghiêm trọng, gọi
                do_call(ADMIN_PHONE);
                is_alerting = 1;
            }
        } else if (iA >= THRESHOLD_FAULT_A + HYSTERESIS_A) {
            // recovery
            if (is_alerting) {
                char msg[160];
                snprintf(msg, sizeof(msg), "Thong bao: Dong da hoi phuc (%.3fA)", iA);
                send_sms_text(ADMIN_PHONE, msg);
                is_alerting = 0;
            }
        }

        last_current = iA;

        // Xử lý SMS vào/ lệnh ON/OFF: (ví dụ, có callback SMS handler - giả sử)
        // Nếu không có callback, poll SMS inbox via AT or API

        Ql_Sleep(2000);
    }
}
