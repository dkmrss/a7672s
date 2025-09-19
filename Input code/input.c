#include "ql_gpio.h"
#include "ql_uart.h"
#include "ql_timer.h"

#define PIN_OUTPUT1  PINNAME_GPIO1   // mapping tới chân OUTPUT1 trong schematic

void proc_main_task(void) {
    // Khởi tạo GPIO làm output, mặc định mức thấp
    Ql_GPIO_Init(PIN_OUTPUT1, PINDIRECTION_OUT, PINLEVEL_LOW, PINPULLSEL_DISABLE);

    while (1) {
        // Bật output
        Ql_GPIO_SetLevel(PIN_OUTPUT1, PINLEVEL_HIGH);
        Ql_Sleep(1000);

        // Tắt output
        Ql_GPIO_SetLevel(PIN_OUTPUT1, PINLEVEL_LOW);
        Ql_Sleep(1000);
    }
}
