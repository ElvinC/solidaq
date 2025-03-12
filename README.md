# SoliDAQ

Super basic ADC/data logger board based on an Adafruit feather rp2040 dev board and an AD7124-4 24-bit ADC.




# Note: The anything below this point haven't actually been implemented yet, they just serve as notes for now.


## Data storage format

There are a maximum of four inputs (three differential, one single-ended 4-20 mA). The following are logged to a shared file:
* 'S' (u8): The byte 0x53, representing the character 'S'. Used for keeping track of used memory.
* log_id (u8): The index of the log, increments after each boot. Starts at 0 and loops around after 255
* time (u32): Current time in milliseconds since previous boot.
* adc_temp (i16): Cold junction temperature in 0.01 deg celsius.
* CH1 (i32): Channel 1 differential voltage (in uV)
* CH2 (i32): Channel 2 differential voltage (in uV)
* CH3 (i32): Channel 3 differential voltage (in uV)
* CH4 (i32): Channel 4 single-ended voltage (in uV)
* Padding to get 32 bytes.

Let's just round up to 32 bytes.

csv representation. Channel voltages in mV
log_id,time,adc_temp,ch1,ch2,ch3,ch4
123,2147483648,23.6,1231.2316,1234.2133,1232.2345,2032.1232
=> 50 characters
Round up to 64 characters with padding.

With 32 megabytes of memory: 2.7 hours of logging time.

When less than 25% of space available:
Switch to 10 Hz logging instead of 100 Hz

Channel measurement format are TBD



## USB Drive mode

The device emulates a FAT16 filesystem.

Some android phones can't open this natively. You can use e.g. the ZUGate app to connect to it.