# SoliDAQ

Super basic ADC/data logger board based on an Adafruit feather rp2040 dev board and an AD7124-4 24-bit ADC.


## Data storage format

```
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
```


csv representation example:

```
id,time[ms],tref[C],lc[mV/V],tc1[C],tc2[C],ps[mA]              
1,1374,21.0,0.670,32.8,20.7,-0.004       
```

With 32 megabytes of memory: 2.7 hours of logging time.


## USB Drive mode

The device emulates a FAT16 filesystem.

Some android phones can't open this natively. You can use e.g. the ZUGate app to connect to it.