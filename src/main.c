// Copyright (c) 2021 nikitapnn1@gmail.com

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <string.h>

#include <avr_firmware/twi.h>
#include <npsys/memtypes.h>

#include </mnt/c/projects/cpp/npsystem/avr_firmware/src/twi.c>

#define DS18B20_DDR 	DDRD
#define DS18B20_PORT 	PORTD
#define DS18B20_PIN 	PIND
#define DS18B20_DQ 		PD4

#include <myavrlib/ds18b20.h>
#include <myavrlib/../../src/ds18b20.c>
#include <myavrlib/../../src/fixed_point.c>

#ifdef UART_TRACE
#	define BAUDRATE 9600
# define USE_RS485
#	include <myavrlib/uart.h>
#	include <myavrlib/../../src/uart.c>
#endif

#include <myavrlib/os/timers.h>

#define TIMERS_MAX 5
#include <myavrlib/../../src/os/timers.c>

// for 16 mhz crystal
#define T500	30
#define T1000	61
#define T1500	93
#define DIFF	10

// states
#define NOT_SYNC		0
#define BEGIN_SYNC	1
#define SYNC				2

#define EQUAL(a, b, diff) (ABS((a), (b)) <= (diff))

__attribute__((__naked__))
int8_t ABS(int8_t a, int8_t b) {
	asm volatile (
	"sub %0, %1 \n\t"
	"brpl _exit \n\t"
	"neg %0 \n\t"
	"_exit: \n\t"
	"ret \n\t"
	::"r"(a),"r"(b)
	);
};

static const uint8_t ws2080_table[] PROGMEM = {
	0x00, 0x31, 0x62, 0x53, 0xc4, 0xf5, 0xa6, 0x97, 0xb9, 0x88, 0xdb, 0xea, 0x7d, 0x4c, 0x1f, 0x2e,
	0x43, 0x72, 0x21, 0x10, 0x87, 0xb6, 0xe5, 0xd4, 0xfa, 0xcb, 0x98, 0xa9, 0x3e, 0x0f, 0x5c, 0x6d,
	0x86, 0xb7, 0xe4, 0xd5, 0x42, 0x73, 0x20, 0x11, 0x3f, 0x0e, 0x5d, 0x6c, 0xfb, 0xca, 0x99, 0xa8,
	0xc5, 0xf4, 0xa7, 0x96, 0x01, 0x30, 0x63, 0x52, 0x7c, 0x4d, 0x1e, 0x2f, 0xb8, 0x89, 0xda, 0xeb,
	0x3d, 0x0c, 0x5f, 0x6e, 0xf9, 0xc8, 0x9b, 0xaa, 0x84, 0xb5, 0xe6, 0xd7, 0x40, 0x71, 0x22, 0x13,
	0x7e, 0x4f, 0x1c, 0x2d, 0xba, 0x8b, 0xd8, 0xe9, 0xc7, 0xf6, 0xa5, 0x94, 0x03, 0x32, 0x61, 0x50,
	0xbb, 0x8a, 0xd9, 0xe8, 0x7f, 0x4e, 0x1d, 0x2c, 0x02, 0x33, 0x60, 0x51, 0xc6, 0xf7, 0xa4, 0x95,
	0xf8, 0xc9, 0x9a, 0xab, 0x3c, 0x0d, 0x5e, 0x6f, 0x41, 0x70, 0x23, 0x12, 0x85, 0xb4, 0xe7, 0xd6,
	0x7a, 0x4b, 0x18, 0x29, 0xbe, 0x8f, 0xdc, 0xed, 0xc3, 0xf2, 0xa1, 0x90, 0x07, 0x36, 0x65, 0x54,
	0x39, 0x08, 0x5b, 0x6a, 0xfd, 0xcc, 0x9f, 0xae, 0x80, 0xb1, 0xe2, 0xd3, 0x44, 0x75, 0x26, 0x17,
	0xfc, 0xcd, 0x9e, 0xaf, 0x38, 0x09, 0x5a, 0x6b, 0x45, 0x74, 0x27, 0x16, 0x81, 0xb0, 0xe3, 0xd2,
	0xbf, 0x8e, 0xdd, 0xec, 0x7b, 0x4a, 0x19, 0x28, 0x06, 0x37, 0x64, 0x55, 0xc2, 0xf3, 0xa0, 0x91,
	0x47, 0x76, 0x25, 0x14, 0x83, 0xb2, 0xe1, 0xd0, 0xfe, 0xcf, 0x9c, 0xad, 0x3a, 0x0b, 0x58, 0x69,
	0x04, 0x35, 0x66, 0x57, 0xc0, 0xf1, 0xa2, 0x93, 0xbd, 0x8c, 0xdf, 0xee, 0x79, 0x48, 0x1b, 0x2a,
	0xc1, 0xf0, 0xa3, 0x92, 0x05, 0x34, 0x67, 0x56, 0x78, 0x49, 0x1a, 0x2b, 0xbc, 0x8d, 0xde, 0xef,
	0x82, 0xb3, 0xe0, 0xd1, 0x46, 0x77, 0x24, 0x15, 0x3b, 0x0a, 0x59, 0x68, 0xff, 0xce, 0x9d, 0xac
};

uint8_t ws2080_crc8(const uint8_t *ptr, uint8_t length) {
	uint8_t crc = 0x00;
	for (uint8_t i = 0; i < length; ++i) {
		crc = pgm_read_byte(&ws2080_table[crc ^ ptr[i]]);
	}
	return crc;
}

typedef struct {
 	uint8_t addr_l;
	struct {
		uint8_t th : 4;
		uint8_t addr_h : 4;
	};
	uint8_t tl;
	uint8_t rh;
	uint8_t wind;
	uint8_t gust;
	uint8_t rain_h;
	uint8_t rain_l;
	uint8_t dir;
	uint8_t crc;
} WS_DATA;

WS_DATA data;
WS_DATA tmp_data;

static volatile uint8_t state;
static uint8_t rv_b;
static uint8_t cnt;

static int8_t process_get_byte(int8_t tm) {
	if (EQUAL(tm, T500, DIFF)) {
		rv_b <<= 1;
		rv_b |= 1;
		cnt++;
	} else if (EQUAL(tm, T1500, DIFF)) {
		rv_b <<= 1;
		cnt++;
	} else if (EQUAL(tm, T1000, DIFF)) {
		// pass
	} else {
		// syncronization loss
		return -1;
	}

	if (cnt == 8) return 0;
	
	return 1;
}

static uint32_t read_cnt;
static uint8_t data_recieved;
static uint8_t crc_error_cnt;

struct {
	Q_u32 read_cnt;
	Q_flt temp;
	Q_u8 hum;
	Q_flt wind;
	Q_flt gust;
	Q_u16 rain_cnt;
	Q_u8 dir;
	Q_flt temp_pot;
} r_seg __attribute__ ((section (".bss.i2c.r.ds_1"))) __attribute__((aligned (32)));

ISR(INT1_vect) {
	static uint8_t ix;
	
	int8_t tm = TCNT2;
	TCNT2 = 0;
	
	if ((TIFR & (1 << TOV2)) || (tm & (1 << 7))) {
		TIFR |= (1 << TOV2);
		state = NOT_SYNC;
		return;
	}
	
	switch(state) {
		case NOT_SYNC:
		{
			if (EQUAL(tm, T500, DIFF)) {
				state = BEGIN_SYNC;
				rv_b = 1;
				cnt = 1;
			}
			break;
		}
		case BEGIN_SYNC:
		{
			int8_t res = process_get_byte(tm);
			if (res == -1) {
				state = NOT_SYNC;
			} else if (res == 0) {
				if (rv_b == 0xFF) {
					rv_b = 0;
					cnt = 0;
					ix = 0;
					state = SYNC;
				} else {
					state = NOT_SYNC;
				}
			} else {
				// do nothing
			}
			break;
		}
		case SYNC:
		{
			int8_t res = process_get_byte(tm);
			if (res == -1) {
				state = NOT_SYNC;
			} else if (res == 0) {
				uint8_t* ptr = (uint8_t*)&tmp_data;
				ptr[ix++] = rv_b;
				rv_b = 0; cnt = 0;
				if (ix == sizeof(WS_DATA)) {
					if (tmp_data.crc == ws2080_crc8(ptr, sizeof(WS_DATA) - 1)) {
						read_cnt++;
						memcpy(&data, &tmp_data, sizeof(WS_DATA));
						state = NOT_SYNC;
						data_recieved = 0x01;
						crc_error_cnt = 0x00;
					} else {
						crc_error_cnt++;
					}
				}
			} else {
				// pass
			}
			break;
		}
	}

}

void rf_reciever_init(void) {
	MCUCR |= (1 << ISC10); // Any logical change on INT1 generates an interrupt request
	GICR |= (1 << INT1); // INT1: External Interrupt Request 1 Enable
	TCCR2 = (1 << CS22) | (1 << CS21); // 256
}

void convert_result (void) {
	if (crc_error_cnt >= 3) {
		memset(&r_seg, 0x00, sizeof(r_seg));
		crc_error_cnt = 0;
		data_recieved = 0x00;
	}
	
	if(!data_recieved) return;
	
	data_recieved = 0x00;
	
	r_seg.read_cnt.value = read_cnt;
	r_seg.read_cnt.quality = VQ_GOOD;
	
	int16_t temp = (int16_t)(data.tl | (data.th << 8));

	r_seg.temp.value = (float)(temp - 400) * 0.1f;
	r_seg.temp.quality = VQ_GOOD;
	
	r_seg.hum.value = data.rh;
	r_seg.hum.quality = VQ_GOOD;
	
	r_seg.wind.value = (float)data.wind * 0.34f;
	r_seg.wind.quality = VQ_GOOD;
	
	r_seg.gust.value = (float)data.gust * 0.34f;
	r_seg.gust.quality = VQ_GOOD;
	
	r_seg.rain_cnt.value = data.rain_h << 8 | data.rain_l;
	r_seg.rain_cnt.quality = VQ_GOOD;
	
	r_seg.dir.value = data.dir;
	r_seg.dir.quality = VQ_GOOD;
};

#define TMR_PROC_BEGIN_CONV 0
#define TMR_PROC_GET_RESULT 1
#define TMR_UART_TRACE 			2

FixedPoint ds_temp;

void temp_proc(void) {
	static uint8_t state = 0;

	if (state == 0) {
		if (timer_expired(TMR_PROC_BEGIN_CONV, 2500 + 1000)) {
			ds18b20_begin_conversion();
			timer_reset(TMR_PROC_GET_RESULT);
			state = 1;
		}
	} else {
		if (timer_expired(TMR_PROC_GET_RESULT, 1000)) {
			state = 0;
			if (ds18b20_get(&ds_temp) != 0) {
				r_seg.temp_pot.quality = VQ_BAD;
			} else {
				r_seg.temp_pot.quality = VQ_GOOD;
				r_seg.temp_pot.value = fptofloat(ds_temp);
			}
		}
	}
}

#ifdef UART_TRACE 
void uart_dump(void) {
	static char buf[16];
	if (timer_expired(TMR_UART_TRACE, 1000)) {
		uart_send(buf, fixtoa(ds_temp, buf, 16));
		uart_send_c('\n');
	}
}
#endif

int main(void) {
	timers_init();
#ifdef UART_TRACE
	uart_init();
	TRACE("UART_TRACE_MODE_ENABLED\n");
	ds18b20_set_max_resolution();
#endif
	rf_reciever_init();
	twi_init();
	
	sei();
	wdt_enable(WDTO_2S);

	while(1) {
		wdt_reset();
		if (state != SYNC) {
			convert_result();
			temp_proc();
#ifdef UART_TRACE
			uart_dump();
#endif
			timers_update();
		}
	}
}