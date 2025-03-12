#include <Arduino.h>

uint32_t flash_current_log_num = 0; //Update this value during initialisation

#include <UsbFileDrive.h>
#include <LittleFS.h>
#include <solidaq.h>
#include <NHB_AD7124.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Adafruit_SPIFlash.h>

#include <pico/mutex.h>

Adafruit_SSD1306 display(128 , 64, &Wire1, -1);

#define LOGO_HEIGHT   16
#define LOGO_WIDTH    16
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
static const unsigned char PROGMEM logo_bmp[] =
{ 0b00000000, 0b11000000,
  0b00000001, 0b11000000,
  0b00000001, 0b11000000,
  0b00000011, 0b11100000,
  0b11110011, 0b11100000,
  0b11111110, 0b11111000,
  0b01111110, 0b11111111,
  0b00110011, 0b10011111,
  0b00011111, 0b11111100,
  0b00001101, 0b01110000,
  0b00011011, 0b10100000,
  0b00111111, 0b11100000,
  0b00111111, 0b11110000,
  0b01111100, 0b11110000,
  0b01110000, 0b01110000,
  0b00000000, 0b00110000 };


int VBAT_PIN = A0;
// For ADC
int ADC_CS = D9;
int SPI1_SCK = D10;
int SPI1_MOSI = D11;
int SPI1_MISO = D12;
int ADC_RDY_MISO = D13; // Tied to D12;

// For flash
int SPI0_SCK = D18;
int SPI0_MOSI = D19;
int SPI0_MISO = D20;
int FLASH1_CS = D25;
int FLASH2_CS = D24;

// Misc pins
int LED_HALF_FULL = D6;
int LED_LOGGING = D1;
int LED_LOWBAT = D0;
int DOWNLOAD_PIN = A3;
int SDA_PIN = D2;
int SCL_PIN = D3;



//Adafruit_FlashTransport_SPI flashTransport(FLASH2_CS, SPI);

//Adafruit_SPIFlash flash(&flashTransport);

solidaq_flash::W25Q128 flash1;
solidaq_flash::W25Q128 flash2;


uint32_t cnt = 0;
bool okayToWrite = true;


Ad7124 adc(ADC_CS, 4000000);


/*
Channel configuration:

REF1: Load cell 3.3V


Channel 0: IN0+ IN1-: Differential load cell (Config 0)
Channel 1: IN2+ IN3-: Thermocouple (Config 1)
Channel 2: IN4+ IN5-: Thermocouple (Config 1)
Channel 3: IN6+ IN7-: 4-20 mA (100r resistor) -> 0.4 - 2V (Config 2)
Channel 4: TEMP, VSS: Internal temperature sensor (Config 3)
*/


#define CH_COUNT 5
#define CONVMODE  AD7124_OpMode_SingleConv
//#define CONVMODE  AD7124_OpMode_Continuous

uint32_t last_log_time;
uint32_t log_interval_ms = 10;
uint32_t log_counter = 0; // ID of the current log
uint32_t loop_counter = 0;
uint32_t flash2_log_start = 65536 * 8;
uint32_t max_logs = 65536 * 8 * 2; // 8 32-byte logs fit in each of the 65536 in the two flash chips. Log 524288 starts on second flash chip
uint32_t display_interval = 10; // Update display at 10 Hz

sensor_log_t current_log; // Current log for data logging purposes
uint8_t raw_log[32]; // The raw log for the memory 
sensor_log_t display_log; // Copy of current log for display on second core

// 0 = not ready
// 1 = standard display
uint8_t display_format = 0;

auto_init_mutex(display_log_mutex); // Mutex for the display log

void erase_all_flash() {
    display.clearDisplay();
    display.setTextSize(1); // Draw 2X-scale text
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 10);
    display.printf("Hold Download\nto confirm\nFlash Erase");
    display.display();

    while (!digitalRead(DOWNLOAD_PIN)) {
        // Wait for release
    }

    int hold_counter = 0;
    while (1) {
        if (digitalRead(DOWNLOAD_PIN)) {
            hold_counter++;
        } else {
            hold_counter = 0;
        }

        sleep_ms(1);
        if (hold_counter > 1000) {
            break;
        }
    }
    display.clearDisplay();
    display.setTextSize(1); // Draw 2X-scale text
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 10);
    display.printf("ERASING IN\n5 SECONDS\nUNPOWER TO ABORT");
    display.display();

    sleep_ms(5000);
    flash1.write_enable();
    sleep_ms(1);
    flash1.chip_erase();
    sleep_ms(1000);
    // Takes 40 to 200 seconds...
    uint32_t erasecounter = 0;
    while (flash1.get_busy()) {
        sleep_ms(1000);
        Serial.printf("Erasing... %ld s elapsed\n", erasecounter++);
        display.clearDisplay();
        display.setTextSize(1); // Draw 2X-scale text
        display.setCursor(10, 10);
        display.printf("Flash 1 erase\n");
        display.setCursor(10, 30);
        display.printf("%d s elapsed\n", erasecounter);
        display.display();
    }

    sleep_ms(1000);
    flash2.write_enable();
    sleep_ms(1);
    flash2.chip_erase();
    sleep_ms(1000);
    // Takes 40 to 200 seconds...
    erasecounter = 0;
    while (flash2.get_busy()) {
        sleep_ms(1000);
        Serial.printf("Erasing... %ld s elapsed\n", erasecounter++);
        display.clearDisplay();
        display.setTextSize(1); // Draw 2X-scale text
        display.setCursor(10, 10);
        display.printf("Flash 2 erase\n");
        display.setCursor(10, 30);
        display.printf("%d s elapsed\n", erasecounter);
        display.display();
    }

    display.clearDisplay();
    display.setTextSize(1); // Draw 2X-scale text
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 10);
    display.printf("FLASH ERASED.\nPlz restart");
    display.display();
    sleep_ms(1000);

    while(1) {

    }
};

// Make the CSV file and give it a simple header
void headerCSV()
{
    File f = LittleFS.open("data.csv", "w");
    f.printf("sample,millis,temp,rand\n");
    f.close();
    cnt = 0;
}

// Called when the USB stick connected to a PC and the drive opened
// Note this is from a USB IRQ so no printing to SerialUSB/etc.
void plug(uint32_t i)
{
    (void)i;
    okayToWrite = false;
}

// Called when the USB is ejected or removed from a PC
// Note this is from a USB IRQ so no printing to SerialUSB/etc.
void unplug(uint32_t i)
{
    (void)i;
    okayToWrite = true;
}

// Called when the PC tries to delete the single file
// Note this is from a USB IRQ so no printing to SerialUSB/etc.
void deleteCSV(uint32_t i)
{
    (void)i;
    erase_all_flash();
    //LittleFS.remove("data.csv");
    //headerCSV();
}

void setup()
{
    Serial.begin();
    sleep_ms(10);

    // Set up pins
    pinMode(VBAT_PIN, INPUT);
    pinMode(DOWNLOAD_PIN, INPUT);
    pinMode(ADC_CS, INPUT);
    pinMode(ADC_RDY_MISO, INPUT);
    pinMode(LED_HALF_FULL, OUTPUT);
    pinMode(LED_LOGGING, OUTPUT);
    pinMode(LED_LOWBAT, OUTPUT);

    pinMode(SPI0_MOSI, OUTPUT);
    pinMode(SPI0_MISO, INPUT);
    pinMode(SPI0_SCK, OUTPUT);

    // SPI for flash
    pinMode(FLASH1_CS, OUTPUT);
    digitalWrite(FLASH1_CS, HIGH);
    pinMode(FLASH2_CS, OUTPUT);
    digitalWrite(FLASH2_CS, HIGH);
    SPI.setTX(SPI0_MOSI);
    SPI.setRX(SPI0_MISO);
    SPI.setSCK(SPI0_SCK);
    SPI.begin(false);
    
    // SPI for ADC
    SPI1.setTX(SPI1_MOSI);
    SPI1.setRX(SPI1_MISO);
    SPI1.setSCK(SPI1_SCK);
    SPI1.setCS(ADC_CS);

    // I2C for OLED
    Wire1.setSCL(SCL_PIN);
    Wire1.setSDA(SDA_PIN);
    Wire1.setClock(400000);

    adc.begin(SPI1);

    flash1.init(SPI, FLASH1_CS);
    flash2.init(SPI, FLASH2_CS);

    uint8_t flash_rxbuf[256];

    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        //for(;;); // Don't proceed, loop forever
    }

    bool is_download_mode = digitalRead(DOWNLOAD_PIN);

    bool chiperase = 0;

    if (chiperase) {
        erase_all_flash();        
    } else {
        display.clearDisplay();
        display.setTextSize(2); // Draw 2X-scale text
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(10, 10);
        display.printf("SoliDAQ\n");
        display.setCursor(10, 30);
        display.setTextSize(1);
        display.printf("FW: 0.1.0-alpha");
        display.setCursor(10, 50);
        display.setTextSize(1);
        if (is_download_mode) {
            display.printf("Initialising download...");
        } else {
            display.printf("Initialising...");
        }
        display.display();
        delay(300); // Pause for 2 seconds
    }


    uint32_t pagecounter = 0;
    bool flash1_available = false;
    // Check flash 1
    for (uint32_t pagenum = 0; pagenum < 65535; pagenum++) {
        // Find first empty page
        flash1.read_data(pagenum << 8, flash_rxbuf, 1);
        
        /*while (1) {
            for (int i = 0; i < 64; i++) {
                Serial.println(flash_rxbuf[i]);
            }
            Serial.println("---");
            sleep_ms(1000);
        }
        */
       pagecounter = pagenum;
        if (flash_rxbuf[0] == 0xFF) {
            flash1_available = true;
            break;
        }
    }
    
    if (!flash1_available && (pagecounter == 65534)) {
        for (uint32_t pagenum = 0; pagenum < 65535; pagenum++) {
            // Find first empty page
            flash2.read_data(pagenum << 8, flash_rxbuf, 1);
            pagecounter = pagenum + 65535;
            if (flash_rxbuf[0] == 0xFF) {
                break;
            }
        }
    }

    //while (1) {
    //    Serial.println(pagecounter);
    //    sleep_ms(1000);
    //}

    if (pagecounter == 0 && !flash1_available) {
        log_counter = max_logs;
    } else {
        log_counter = pagecounter * 8;
    }

    flash_current_log_num = log_counter;


    if (digitalRead(DOWNLOAD_PIN)) {
        // Emulate flash chip
        usbFileDrive.onDelete(deleteCSV);
        usbFileDrive.onPlug(plug);
        usbFileDrive.onUnplug(unplug);
        usbFileDrive.begin("data.csv", "data.csv");


        /*
        sleep_ms(3000);
        Serial.println("Outputting data :3");
        for (uint32_t log_num = 0; log_num < max_logs; log_num++) {
            if (log_num < flash2_log_start) {
                flash1.read_data(log_num * 32, (uint8_t *) &current_log, 32);
            } else {
                flash2.read_data((log_num - flash2_log_start) * 32, (uint8_t *) &current_log, 32);
            }

            sleep_ms(20);
            Serial.printf("#%ld, t: %ld , CH0: %f mV/V\n", log_num, current_log.time, ((double) current_log.lc) / 1000000.0);
        }
        */


        display.clearDisplay();
        display.setTextSize(2); // Draw 2X-scale text
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(10, 10);
        display.printf("SoliDAQ\n");
        display.setCursor(10, 30);
        display.setTextSize(1);
        display.printf("FW: 0.1.0-alpha");
        display.setCursor(10, 50);
        display.setTextSize(1);
        display.printf("Download mode");
        display.display();
        delay(300); // Pause for 2 seconds

        while (1) {
            // Download mode
        }
    }




    // The filter select bits determine the filtering and ouput data rate
    // 1 = Minimum filter, Maximum sample rate
    // 2047 = Maximum filter, Minumum sample rate
    int filterSelBits = 6; // freq = 614.4 kHz / (32 * filterSelBits)

    adc.setAdcControl(CONVMODE, AD7124_FullPower, true);

    // Setup 0, load cell or bridges, ratiometric
    adc.setup[0].setConfig(AD7124_Ref_ExtRef1, AD7124_Gain_64, true, AD7124_Burnout_Off, 3.3);
    adc.setup[0].setFilter(AD7124_Filter_SINC3, filterSelBits);

    // Setup 1, Thermocouples
    adc.setup[1].setConfig(AD7124_Ref_Internal, AD7124_Gain_64, true);
    adc.setup[1].setFilter(AD7124_Filter_SINC3, filterSelBits);

    // Setup 2, Pressure sensor
    adc.setup[2].setConfig(AD7124_Ref_Internal, AD7124_Gain_1, true);
    adc.setup[2].setFilter(AD7124_Filter_SINC3, filterSelBits);

    // Setup 3, Internal temp
    adc.setup[3].setConfig(AD7124_Ref_Internal, AD7124_Gain_1, true);
    adc.setup[3].setFilter(AD7124_Filter_SINC3, filterSelBits);

    // Channels
    adc.setChannel(0, 0, AD7124_Input_AIN0, AD7124_Input_AIN1, true); // Loadcell
    adc.setChannel(1, 1, AD7124_Input_AIN2, AD7124_Input_AIN3, true); // Thermocouple
    adc.setChannel(2, 1, AD7124_Input_AIN4, AD7124_Input_AIN5, true); // Thermocouple
    adc.setChannel(3, 2, AD7124_Input_AIN6, AD7124_Input_AIN7, true); // Pressure
    adc.setChannel(4, 2, AD7124_Input_TEMP, AD7124_Input_AVSS, true); // Internal temp
    
    // Thermocouple bias on negative
    adc.setVBias(AD7124_VBias_AIN3,true); 
    adc.setVBias(AD7124_VBias_AIN5,true); 


    delay(1000);


   last_log_time = millis();
}


float get_vbat() {
    float vbat = analogRead(VBAT_PIN);
    return vbat / 1024.0 * 3.3 * 2.0;
}

bool get_download_btn() {
    return digitalReadFast(DOWNLOAD_PIN);
}

uint8_t flash_buffer[2048];
int32_t tc1_avg = 0.0;
int32_t tc2_avg = 0.0;
double ambient = 23.0; // Ambient temp, track glitch when less than -40 and more than 50
void loop() {

    while (millis() < last_log_time + log_interval_ms) {
        // wait
    }
    long current_time = millis();
    long delta_time = current_time - last_log_time;
    last_log_time = current_time;

    Ad7124_Readings readings[CH_COUNT];  


    adc.readVolts(readings,CH_COUNT);

    float pico_temp = analogReadTemp(3.3);

    // Apply scalings
    Serial.println(readings[2].value * 1000, 5);
    readings[4].value = adc.scaleIcTemp(readings[4].value) - 2.0; // Usually warms up slightly
    if (readings[4].value < -40 || readings[4].value > 50) {
        // Internal temp sometimes glitches, use prev value
        readings[4].value = ambient;
    }
    ambient = readings[4].value;

    readings[0].value = adc.scaleFB(readings[0].value, 3.3, 1.00);
    if (readings[1].value > 0.0206) {
        readings[1].value = 500.0;
    } else if (readings[1].value < -0.007) {
        readings[1].value = -273.0;
    } else {
        readings[1].value = adc.scaleTC(readings[1].value,readings[4].value, Type_K);
    }

    if (readings[2].value > 0.0206) {
        readings[2].value = 500.0;
    } else if (readings[2].value < -0.007) {
        readings[2].value = -273.0;
    } else {
        //readings[2].value *= 1000;
        readings[2].value = adc.scaleTC(readings[2].value,readings[4].value, Type_K);
    }
    

    //Serial.printf("CH0: %.2f mV/V, CH1: %.2f degC, CH2: %.2f degC, CH3: %.2f V, CH4: %.2f degC\n", readings[0].value, readings[1].value, readings[2].value, readings[3].value, readings[4].value);

    double lc = readings[0].value;
    double tc1 = readings[1].value;
    double tc2 = readings[2].value;
    double ps = readings[3].value * 10; // Current in mA
    double tref = readings[4].value;
    float vbat = get_vbat();

    current_log.adc_temp = (int16_t) (tref * 100.0);
    current_log.core_temp = (int16_t) (pico_temp * 100.0); // TODO: Update with pico temp
    current_log.header = 'S';
    current_log.lc = (int32_t) (lc * 1000000.0);
    current_log.log_id = 1; // Test with 1 for now. TODO
    current_log.ps = (int32_t) (ps * 1000000.0);
    current_log.tc1 = (int32_t) (tc1 * 100.0); // Temp in 0.01 deg celsius
    current_log.tc2 = (int32_t) (tc2 * 100.0);
    current_log.time = millis();
    current_log.delta_time = (uint8_t) delta_time;
    current_log.log_percent = (uint8_t) (((float)log_counter * 100) / (float)max_logs);
    current_log.vbat = (uint16_t) (vbat * 100.0);

    tc1_avg = tc1_avg * 0.95 + current_log.tc1 * 0.05;
    tc2_avg = tc2_avg * 0.95 + current_log.tc2 * 0.05;

    //sleep_us(50);
    // Write data to flash
    if (log_counter < flash2_log_start) {
        flash1.write_enable();
        sleep_us(10);
        flash1.page_program(log_counter * 32, (uint8_t*) &current_log, 32);
        //sleep_us(500); // approx flash write time
    } else if (log_counter < max_logs) {
        flash2.write_enable();
        sleep_us(10);
        flash2.page_program((log_counter - flash2_log_start) * 32, (uint8_t*) &current_log, 32);
        //sleep_us(500); // approx flash write time
         //Expected program time 434 microseconds
    }
   


    if (loop_counter % display_interval == 0) {
        Serial.printf("Log num: %ld out of %ld\n", log_counter, max_logs);
        // Update the shared variable
        if (mutex_try_enter(&display_log_mutex, 0)) {
            display_log = current_log;
            display_log.tc1 = tc1_avg;
            display_log.tc2 = tc2_avg;
            display_format = 1;
            mutex_exit(&display_log_mutex);
        }
    } 
    if (log_counter < max_logs) {
        log_counter++;
    }
    loop_counter++;

    digitalWriteFast(LED_LOGGING, (log_counter % 100) < 5);
    digitalWriteFast(LED_LOWBAT, vbat < 3.85);
    digitalWriteFast(LED_HALF_FULL, log_counter > flash2_log_start);
}

// 2nd core 
void update_display(double lc, double tc1, double tc2, double ps, double tref, float vbat,int delta_time, uint8_t log_percent) {
    display.clearDisplay();
    int log_id = 12;

    display.setTextSize(1); // Draw 2X-scale text
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.printf("%.2fV | %d%% | dt:%d\n", vbat, log_percent, delta_time);
    display.drawLine(0,8,128,8,1);
    display.setCursor(0,display.getCursorY()+3);
    display.printf("LC [mV/V]: %.3f\n", lc);
    display.setCursor(0,display.getCursorY()+2);
    display.printf("TC1 [C]: %.2f\n", tc1);
    display.setCursor(0,display.getCursorY()+2);
    display.printf("TC2 [C]: %.2f\n", tc2);
    display.setCursor(0,display.getCursorY()+2);
    display.printf("PS [mA]: %.2f\n", ps);
    display.setCursor(0,display.getCursorY()+2);
    display.printf("Tref [C]: %.2f\n", tref);
    display.display();
}

// 2nd core for display handling
void setup1() { 
    sleep_ms(1000);
}

// 2nd core for display handling
sensor_log_t core2_log;
uint8_t core2_display_format = 0;
void loop1() {
    sleep_ms(100);

    if (mutex_enter_timeout_ms(&display_log_mutex,10)) {
        core2_log = display_log;
        core2_display_format = display_format;
        mutex_exit(&display_log_mutex);
    }

    if (core2_display_format == 1) {
        double lc = ((double) core2_log.lc) / 1000000L;
        double tc1 = ((double) core2_log.tc1) / 100L;
        double tc2 = ((double) core2_log.tc2) / 100L;
        double ps = ((double) core2_log.ps) / 1000000L;
        double tref = ((double) core2_log.adc_temp) / 100L;
        float vbat = ((float) core2_log.vbat) / 100.f;
        int delta_time = (int) core2_log.delta_time;
        uint8_t log_percent = core2_log.log_percent;

        update_display(lc, tc1, tc2, ps, tref, vbat, (int) delta_time, log_percent);
    }
}