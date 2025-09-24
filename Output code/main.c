#include "simcom_api.h"
#include "simcom_gpio.h"
#include "simcom_pm.h"
#include "simcom_sms.h"
#include "simcom_call.h"
#include "simcom_network.h"
#include "simcom_uart.h"
#include "simcom_debug.h"
#include "simcom_os.h"
#include "simcom_flash.h"
#include "simcom_rtc.h"
#include "simcom_common.h"

#define GPIO_INPUT1 5    // CELL_IO5 (INPUT1)
#define GPIO_OUTPUT1 13  // CELL_IO13 (OUTPUT1)
#define GPIO_RESET 18    // CELL_IO18 (RESET)
#define GPIO_EN_CHARGE 17 // CELL_IO17 (EN_CHARGE)
#define GPIO_PW_DET 8    // CELL_IO8 (PW_DET)
#define ADC_SENSOR 0     // ADC channel for current sensor
#define LED_INDICATOR 9  // Assuming LTE_IND or similar for status LED

#define TASK_STACK_SIZE  (1024 * 5)
#define TASK_PRIORITY    150

#define SMS_BUFFER_SIZE  256
#define PHONE_BUFFER_SIZE 20

#define CURRENT_THRESHOLD 100  // Example threshold in mV for detecting current loss (adjust based on sensor)
#define BAT_LOW_THRESHOLD 3850 // mV
#define BAT_HIGH_THRESHOLD 4150 // mV
#define VOLTAGE_DIVIDER 4      // Adjust based on divider ratio (e.g., 133/33 ~4)

#define DELAY_MS(ms) sAPI_TaskSleep((ms) / 10)  // 1 tick = 10ms

static sTaskRef mainTask = NULL;
static UINT8 mainTaskStack[TASK_STACK_SIZE];

static char adminPhone[PHONE_BUFFER_SIZE] = "0917868070";  // Default admin phone
static char alertMessage[] = "Canh Bao: Dong dien bi ngat dot ngot!";

static int outputState = 0;  // 0: OFF, 1: ON
static int powerDetected = 1;  // Assume power on initially
static int charging = 0;       // 0: Not charging, 1: Charging
static int errorFlag = 0;      // For current loss detection

void initGpio(void) {
    SC_GPIOConfiguration gpioCfg;

    // Output pins
    gpioCfg.pinDir = SC_GPIO_OUT_PIN;
    gpioCfg.initLv = 0;
    gpioCfg.pinPull = SC_GPIO_PULLUP_ENABLE;
    gpioCfg.pinEd = SC_GPIO_NO_EDGE;
    gpioCfg.isr = NULL;
    gpioCfg.wu = NULL;

    sAPI_GpioConfig(GPIO_OUTPUT1, gpioCfg);
    sAPI_GpioConfig(GPIO_EN_CHARGE, gpioCfg);
    sAPI_GpioConfig(LED_INDICATOR, gpioCfg);

    sAPI_GpioSetValue(GPIO_OUTPUT1, 0);  // OFF
    sAPI_GpioSetValue(GPIO_EN_CHARGE, 1); // Charge off (assuming active low)
    sAPI_GpioSetValue(LED_INDICATOR, 1); // ON for init

    // Input pins
    gpioCfg.pinDir = SC_GPIO_IN_PIN;
    gpioCfg.initLv = 1;
    gpioCfg.pinPull = SC_GPIO_PULLUP_ENABLE;

    sAPI_GpioConfig(GPIO_INPUT1, gpioCfg);
    sAPI_GpioConfig(GPIO_PW_DET, gpioCfg);
    sAPI_GpioConfig(GPIO_RESET, gpioCfg);
}

void handleSms(SC_SmsMessageInfo *sms) {
    char msg[SMS_BUFFER_SIZE];
    memcpy(msg, sms->data, sms->length);
    msg[sms->length] = '\0';

    if (strstr(msg, "ON")) {
        sAPI_GpioSetValue(GPIO_OUTPUT1, 1);
        outputState = 1;
        sAPI_Debug("Output ON via SMS");
    } else if (strstr(msg, "OFF")) {
        sAPI_GpioSetValue(GPIO_OUTPUT1, 0);
        outputState = 0;
        sAPI_Debug("Output OFF via SMS");
    }

    // Send confirmation SMS
    sAPI_SmsSend(sms->oa, "Command received. Output state: %s", outputState ? "ON" : "OFF");
}

void monitorCurrent(void) {
    if (outputState == 1) {  // Only monitor if output is ON
        UINT32 adcValue = sAPI_ReadAdc(ADC_SENSOR);
        adcValue *= VOLTAGE_DIVIDER;  // Adjust for divider
        sAPI_Debug("Current ADC: %d mV", adcValue);

        if (adcValue < CURRENT_THRESHOLD && errorFlag == 0) {
            errorFlag = 1;
            sAPI_SmsSend(adminPhone, alertMessage);
            sAPI_CallDial(adminPhone);  // Call for emergency
            sAPI_Debug("Current loss detected! Alert sent.");
        } else if (adcValue >= CURRENT_THRESHOLD) {
            errorFlag = 0;
        }
    }
}

void monitorPowerAndBattery(void) {
    int pwDet = sAPI_GpioGetValue(GPIO_PW_DET);
    if (pwDet == 0 && powerDetected == 1) {  // Power loss
        powerDetected = 0;
        sAPI_SmsSend(adminPhone, "Canh Bao: Mat dien!");
        sAPI_CallDial(adminPhone);
        sAPI_Debug("Power loss detected.");
    } else if (pwDet == 1 && powerDetected == 0) {
        powerDetected = 1;
        sAPI_SmsSend(adminPhone, "Canh Bao: Co dien tro lai.");
        sAPI_Debug("Power restored.");
    }

    UINT32 vbat = sAPI_ReadVbat() * 1000;  // Assuming in uV, convert to mV
    sAPI_Debug("VBAT: %d mV", vbat);

    if (powerDetected == 1) {  // Only charge if power is present
        if (vbat <= BAT_LOW_THRESHOLD && charging == 0) {
            charging = 1;
            sAPI_GpioSetValue(GPIO_EN_CHARGE, 0);  // Enable charge (active low)
            sAPI_Debug("Starting charge.");
        } else if (vbat >= BAT_HIGH_THRESHOLD && charging == 1) {
            charging = 0;
            sAPI_GpioSetValue(GPIO_EN_CHARGE, 1);  // Disable charge
            sAPI_Debug("Charge complete.");
        }
    } else {
        sAPI_GpioSetValue(GPIO_EN_CHARGE, 1);  // Disable charge on power loss
    }
}

void smsCallback(SC_SmsMessageInfo *sms) {
    handleSms(sms);
}

void mainTaskFunc(void *arg) {
    sAPI_SmsInit();
    sAPI_SmsSetNewMessageCallback(smsCallback);

    while (1) {
        monitorCurrent();
        monitorPowerAndBattery();

        // Blink LED for status
        sAPI_GpioSetValue(LED_INDICATOR, 1);
        DELAY_MS(500);
        sAPI_GpioSetValue(LED_INDICATOR, 0);
        DELAY_MS(500);
    }
}

void app_main(void) {
    initGpio();

    if (sAPI_TaskCreate(&mainTask, mainTaskStack, TASK_STACK_SIZE, TASK_PRIORITY, "Main Task", mainTaskFunc, NULL) != SC_SUCCESS) {
        sAPI_Debug("Failed to create main task");
    }

    // Watchdog setup (from sample)
    sAPI_SetWtdTimeOutPeriod(0x08);  // 256s period
    sAPI_FalutWakeEnable(1);
    sAPI_SoftWtdEnable(1);
    sAPI_FeedWtd();
}