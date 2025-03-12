#ifndef SOLIDAQ_H
#define SOLIDAQ_H

#include <Arduino.h>
#include <SPI.h>

// Sensor log, both for logging and for display transfer
typedef struct SensorLog {
    uint8_t header; // 'S' = 0x53. Keep track of used memory
    uint8_t log_id; // Log ID and marker for used memory.
    uint32_t time; // Time in ms
    int16_t adc_temp; // Cold junction temp in 0.01 deg celsius
    int16_t core_temp; // rp2040 temp in 0.01 deg celsius
    int32_t lc; // Loadcell, ratiometric in millionth (0.000001)
    int32_t tc1; // Thermocouple 1, value in nanovolts
    int32_t tc2; // Thermocouple 2, value in nanovolts
    int32_t ps; // 4-20 mA pressure. Value in nanoamps
    uint8_t delta_time;
    uint8_t log_percent; // Percent of the log that is full
    uint16_t vbat; // Battery voltage in 0.01V
} sensor_log_t;

/*
namespace SolidaqThermocouple {
    const float t_coeffs0[] = {0.00E+00, 3.87E-02, 4.42E-05, 1.18E-07, 2.00E-08, 9.01E-10, 2.27E-11, 3.61E-13, 3.85E-15, 2.82E-17, 1.43E-19, 4.88E-22, 1.08E-24, 1.39E-27, 7.98E-31};
    const float t_coeffs1[] = {0.00E+00, 3.87E-02, 3.33E-05, 2.06E-07, -2.19E-09, 1.10E-11, -3.08E-14, 4.55E-17, -2.75E-20};

    float t_inv_coeffs0[] = { 0.0000000E+00,2.5949192E+01,-2.1316967E-01,7.9018692E-01,4.2527777E-01,1.3304473E-01,2.0241446E-02,1.2668171E-03 };
    const float t_inv_coeffs1[] = { 0.000000E+00,2.592800E+01,-7.602961E-01,4.637791E-02,-2.165394E-03,6.048144E-05,-7.293422E-07,0.000000E+00 };


    float temperatureToVoltage_T(float T) {
        float voltage = 0;
        float t_times = 1;

        if (T < 0) {
            for (int i = 0; i < sizeof(t_coeffs0)/sizeof(float); i++) {
                voltage += t_coeffs0[i] * t_times;
                t_times = t_times * T;
            }
        } else {
            for (int i = 0; i < sizeof(t_coeffs1)/sizeof(float); i++) {
                voltage += t_coeffs1[i] * t_times;
                t_times = t_times * T;
            }
        }

        return voltage;
    }
}
*/
namespace solidaq_flash {
    // Generic minimal spiflash driver based on the W25Q128 datasheet and https://github.com/raspberrypi/pico-examples/blob/master/pio/spi/spi_flash.c
    // https://www.mouser.com/datasheet/2/949/w25q128jv_revf_03272018_plus-1489608.pdf
    class W25Q128 {

        public:

        SPISettings spiSettings;

        uint32_t pagesize = 256;
        uint32_t sectorsize = 4096;

        const uint8_t FLASH_CMD_PAGE_PROGRAM = 0x02;
        const uint8_t FLASH_CMD_READ         = 0x03;
        const uint8_t FLASH_CMD_STATUS       = 0x05;
        const uint8_t FLASH_CMD_WRITE_EN     = 0x06;
        const uint8_t FLASH_CMD_SECTOR_ERASE = 0x20;

        const uint8_t FLASH_STATUS_BUSY_MASK = 0x01;


        SPIClass *spiport;
        uint cs_pin;

        uint8_t JEDEC_MF;
        uint16_t JEDEC_DEV;

        bool error; // true if an error has occured

        void wait() {
            asm volatile("nop"); // One cycle, about 7.5 ns
            // Timing requirements:
            // CS relative to clock: 3 nanoseconds
            // CS deselect for read: 10 ns
            // CS deselect for write: 50 ns
        }

        void set_cs(bool val) {
            this->wait();
            gpio_put(this->cs_pin, val);
            this->wait();
        }

        void spi_write_read(uint8_t *txbuf, uint8_t *rxbuf, size_t len) {
            this->spiport->beginTransaction(this->spiSettings);
            this->set_cs(0);
            for (uint i = 0; i < len; i++) {
                rxbuf[i] = this->spiport->transfer(txbuf[i]);
            }
            this->set_cs(1);
            this->spiport->endTransaction();
        }

        uint8_t read_status_1() {
            //Read Status Register-1: 05h, (S7-S0)
            uint8_t txbuf[2] = {0x05, 0x00};
            uint8_t rxbuf[2];
            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));

            return rxbuf[1];
        }

        /**
         * @brief Check if a read or write is in progress
         */
        bool get_busy() {
            return this->read_status_1() & 0x01; // Bit 0 is the busy flag
        }

        void write_enable() {
            uint8_t txbuf[1] = {0x06};
            uint8_t rxbuf[1];
            this->spi_write_read(txbuf, rxbuf, 1);
        }

        void write_disable() {
            uint8_t txbuf[1] = {0x04};
            uint8_t rxbuf[1];
            this->spi_write_read(txbuf, rxbuf, 1);
        }


        uint8_t jedec_id() {
            uint8_t txbuf[4]; //{cmd, ..}}
            uint8_t rxbuf[4]; //{dummy, (MF7-MF0), (ID15-ID8), (ID7-ID0)}
            txbuf[0] = 0x9F;

            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));

            this->JEDEC_MF = rxbuf[1];

            this->JEDEC_DEV = (((uint16_t) rxbuf[2]) << 8) | ((uint16_t) rxbuf[3]);
            return this->JEDEC_MF;
        }


        void read_data(uint32_t addr, uint8_t *buffer, uint8_t len) {
            // This one can go up to 50 MHz
            // 03h A23-A16 A15-A8 A7-A0 (D7-D0)
            uint8_t txbuf[4] = {0x03, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};

            this->spiport->beginTransaction(this->spiSettings);
            this->set_cs(0);
            for (uint i = 0; i < 4; i++) {
                this->spiport->transfer(txbuf[i]);
            }
            for (uint i = 0; i < len; i++) {
                buffer[i] = this->spiport->transfer(0x00);
            }
            this->set_cs(1);
            this->spiport->endTransaction();

        }

        void page_program(uint32_t addr, uint8_t *buffer, uint8_t len) {
            // The 8 LSB's of the address should be zero if starting at start of page
            // A full page program takes about 200 microseconds at 10 MHz.
            // Page Program 02h, A23-A16, A15-A8, A7-A0, D7-D0, D7-D0...
            uint8_t txbuf[4] = {0x02, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
            uint8_t rxbuf[4];

            // If an entire 256 byte page is to be programmed, the last address byte (the 8 least significant address bits)
            // should be set to 0.
            this->spiport->beginTransaction(this->spiSettings);
            this->set_cs(0);
            for (uint i = 0; i < 4; i++) {
                rxbuf[i] = this->spiport->transfer(txbuf[i]);
            }
            for (uint i = 0; i < len; i++) {
                this->spiport->transfer(buffer[i]);
            }
            this->set_cs(1);
            this->spiport->endTransaction();

        }

        void erase_4kb(uint32_t addr) {
            // Sector Erase (4KB) 20h, A23-A16, A15-A8, A7-A0
            uint8_t txbuf[4] = {0x20, (uint8_t) (addr >> 16), (uint8_t) (addr >> 8), (uint8_t)addr};
            uint8_t rxbuf[4];
            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));
        }

        void erase_32kb(uint32_t addr) {

        }

        void erase_64kb(uint32_t addr) {

        }

        /**
         * @brief Erase whole chip (results in 1's)
         */
        void chip_erase() {
            uint8_t txbuf[1] = {0xC7};
            uint8_t rxbuf[1];
            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));
        }

        bool init(SPIClass &spiport, uint cs_pin) {
            this->spiSettings = arduino::SPISettings( 1000000, BitOrder::MSBFIRST, SPIMode::SPI_MODE0 );

            this->spiport = &spiport;
            this->cs_pin = cs_pin;
            return true;
        }
    };

    // Minimal driver for the 1Gbit ZD35Q1GC-IB flash chip
    // https://www.lcsc.com/datasheet/lcsc_datasheet_2201121400_Zetta-ZD35Q1GC-IB_C2928927.pdf1
    // 2048 bytes per page (+64 extra bytes)
    // 64 pages per blocks
    // 1024 blocks
    class ZD35Q1GC {

        public:

        SPISettings spiSettings;

        uint32_t pagesize = 2048;
        uint32_t sectorsize = 131072;

        SPIClass *spiport;
        uint cs_pin;

        uint8_t JEDEC_MF;
        uint16_t JEDEC_DEV;

        bool error; // true if an error has occured

        void wait() {
            delayMicroseconds(1); // One cycle, about 7.5 ns
            // Timing requirements:
            // CS relative to clock: 3 nanoseconds
            // CS deselect for read: 10 ns
            // CS deselect for write: 50 ns
        }

        void set_cs(bool val) {
            this->wait();
            gpio_put(this->cs_pin, val);
            this->wait();
        }

        void spi_write_read(uint8_t *txbuf, uint8_t *rxbuf, size_t len) {
            this->spiport->beginTransaction(this->spiSettings);
            this->set_cs(0);
            for (uint i = 0; i < len; i++) {
                rxbuf[i] = this->spiport->transfer(txbuf[i]);
            }
            this->set_cs(1);
            this->spiport->endTransaction();
        }

        void write_enable() {
            uint8_t txbuf[1] = {0x06};
            uint8_t rxbuf[1];
            this->spi_write_read(txbuf, rxbuf, 1);
        }

        void write_disable() {
            uint8_t txbuf[1] = {0x04};
            uint8_t rxbuf[1];
            this->spi_write_read(txbuf, rxbuf, 1);
        }

        // Read status register from Features
        uint8_t read_status_1() {
            uint8_t txbuf[3] = {0x0F, 0xC0, 0x00};
            uint8_t rxbuf[3];
            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));

            return rxbuf[2];
        }

        uint8_t read_protection() {
            uint8_t txbuf[3] = {0x0F, 0xA0, 0x00};
            uint8_t rxbuf[3];
            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));

            return rxbuf[2];
        }

        void set_no_protection() {
            uint8_t txbuf[3] = {0x1F, 0xA0, 0x00};
            uint8_t rxbuf[3];
            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));
        }

        /**
         * @brief Check if a read or write is in progress
         */
        bool get_busy() {
            return this->read_status_1() & 0x01; // Bit 0 is the busy flag. "OIP" = operation in progress
        }

        void page_read_to_cache(uint32_t addr) {
            uint8_t txbuf[4] = {0x13, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
            uint8_t rxbuf[4];

            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));
        }

        void read_from_cache(uint16_t addr, uint8_t *buffer, uint16_t len) {
            uint8_t wrap = 0b01000000; // 2048 bit wrap
            uint8_t txbuf[4] = {0x03, ((uint8_t)(addr >> 8) & 0b00001111) | wrap, (uint8_t)addr, 0x00};

            this->spiport->beginTransaction(this->spiSettings);
            this->set_cs(0);
            for (uint i = 0; i < 4; i++) {
                this->spiport->transfer(txbuf[i]);
            }
            for (uint i = 0; i < len; i++) {
                buffer[i] = this->spiport->transfer(0x00);
            }
            this->set_cs(1);
            this->spiport->endTransaction();
        }


        uint8_t jedec_id() {
            uint8_t txbuf[4]; //{cmd, ..}}
            uint8_t rxbuf[4]; //{dummy, (MF7-MF0), (ID15-ID8), (ID7-ID0)}
            txbuf[0] = 0x9F;
            txbuf[1] = 0x00;

            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));

            this->JEDEC_MF = rxbuf[2];

            this->JEDEC_DEV = rxbuf[3];
            return this->JEDEC_MF;
        }

        // Read data. Returns true if succesful
        bool read_data_blocking(uint32_t addr, uint8_t *buffer, uint16_t len) {
            // Start by loading into cache, then reading from cache
            // Address with 27 bit number
            // Align with start of page when possible (bits 0-10 = 0)
            uint32_t row_addr = addr >> 11;
            uint16_t column_addr = addr & 0b0000011111111111;

            this->page_read_to_cache(row_addr);
            delayMicroseconds(10);
            int timeout = 0;
            while (this->get_busy()) {
                delayMicroseconds(10);
                timeout++;
                if (timeout > 200) { // Timeout after 2 ms
                    return false;
                }

            }
            this->read_from_cache(column_addr,buffer,len);
            return true;
        }

        void program_load(uint16_t column_addr, uint8_t *buffer, uint16_t len) {
            uint8_t txbuf[3] = {0x02, ((uint8_t)(column_addr >> 8) & 0b00001111), (uint8_t)column_addr};

            this->spiport->beginTransaction(this->spiSettings);
            this->set_cs(0);
            for (uint i = 0; i < 3; i++) {
                this->spiport->transfer(txbuf[i]);
            }
            for (uint i = 0; i < len; i++) {
                this->spiport->transfer(buffer[i]);
            }
            this->set_cs(1);
            this->spiport->endTransaction();
        }

        void program_execute(uint32_t row_addr) {
            uint8_t txbuf[4] = {0x10, (uint8_t)(row_addr >> 16), (uint8_t)(row_addr >> 8), (uint8_t)row_addr};
            uint8_t rxbuf[4];

            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));
        }

        bool write_data_blocking(uint32_t addr, uint8_t *buffer, uint16_t len) {
            uint32_t row_addr = addr >> 11;
            uint16_t column_addr = addr & 0b0000011111111111;
            Serial.println(row_addr);
            Serial.println(column_addr);

            this->program_load(column_addr, buffer, len);
            delayMicroseconds(10);
            this->write_enable();
            delayMicroseconds(10);
            Serial.print("Status before:");
            Serial.println(this->read_status_1(),HEX);
            this->program_execute(row_addr);
            delayMicroseconds(10);
            int timeout = 0;
            while (this->get_busy()) {
                delayMicroseconds(10);
                timeout++;
                if (timeout > 200) { // Timeout after 2 ms
                    Serial.println("TIMEOUT");
                    return false;
                }
            }
            Serial.println(this->read_status_1(),HEX);
            return !(this->read_status_1() & 0b00001000); // Check pfail bit
        }

        void block_erase(uint32_t row_addr) {
            uint8_t txbuf[4] = {0xD8, (uint8_t)(row_addr >> 16), (uint8_t)(row_addr >> 8), (uint8_t)row_addr};
            uint8_t rxbuf[4];

            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));
        }

        /**
         * @brief Erase whole chip (results in 1's)
         */
        void chip_erase() {
            uint8_t txbuf[1] = {0xC7};
            uint8_t rxbuf[1];
            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));
        }

        void reset() {
            uint8_t txbuf[1] = {0xFF};
            uint8_t rxbuf[1];
            this->spi_write_read(txbuf, rxbuf, sizeof(txbuf));
        }

        bool init(SPIClass &spiport, uint cs_pin) {
            this->spiSettings = arduino::SPISettings( 1000000, BitOrder::MSBFIRST, SPIMode::SPI_MODE0 );

            this->spiport = &spiport;
            this->cs_pin = cs_pin;
            this->reset();
            delay(5);
            Serial.print("Initial status: 0x");
            Serial.print(this->read_status_1(), HEX);
            delay(1);
            this->set_no_protection();
            return true;
        }
    };


}


#endif /* SOLIDAQ_H */