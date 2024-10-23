
#include <avr/io.h>

#define F_CPU 2000000
#include <util/delay.h>
#include <avr/interrupt.h>

/* pinout:
 *
 *                                     _____
 *                 1 - \reset | pb5  -|o    |-  vcc  - 8
 * SOLENOID_PIN <- 2 - pb3           -|     |-  pb2  - 7  <- SCK_PIN
 *    SERVO_PIN <- 3 - pb4           -|     |-  pb1  - 6  -> MISO_PIN
 *                 4 - gnd           -|_____|-  pb0  - 5  <- MOSI_PIN
 *
 *  SERVO_PIN [OUT]    - PB1
 *  SCK_PIN   [IN]     - PB2
 *  MOSI_PIN  [IN]     - PB0
 *  SOLENOID_PIN [OUT] - PB3
 *
 * programming pins:
 *  \reset - pb5
 *  sck    - pb2
 *  miso   - pb1
 *  mosi   - pb0
 */

#define MISO_PIN     PB1
#define SERVO_PIN    PB4
#define SOLENOID_PIN PB3

/* EEPROM
 * +------+--------------------------+
 * | 0x00 | Magic.                   |
 * | 0x01 | Device ID.               |
 * | 0x02 | Initial PWM duty.        |
 * +------+--------------------------+
 */
 
#define EEPROM_MAGIC    0
#define EEPROM_DEV_ID   1
#define EEPROM_PWM_DUTY 2

#define MAGIC   0x43

/* PWM duty cycle
 *
 * Should be from around 800us to around 2200us
 * Timer 1 clock = 1MHz (sys clock) / 32 (timer1 prescaler) = 31250Hz
 * Timer increments every 1/f = 31us
 * PWM output set to HIGH when timer's counter = 0, set to LOW when timer's counter = OCR1B
 *   where OCR1B belongs to [PWM_MIN..PWM_MAX]
 * Timer reset on timer_counter = OCR1C (250)
 */
#define PWM_MIN 25             /* ~780us  if timer clocked with 31.25kHz */
#define PWM_MAX 68             /* ~2153us if timer clocked with 31.25kHz */
#define PWM_TIMER_RESET 250    /* PWM out frequency - 128.2Hz - if timer clked at 31.25kHz */

/* SPI slave communication 
 * 
 * No slave select - use device id as slave address
 * 
 * Frame format:
 *
 * +-------------------+--------+--------+-----------+----------+
 * | START_BYTE (0x69) | DEV_ID | CMD_ID | PARAMETER | CHECKSUM |
 * +-------------------+--------+--------+-----------+----------+
 */

#define START_BYTE  0x69

#define CMD_PWM_DUTY 1  /* Change PWM duty command, parameter : [PWM_MIN..PWM_MAX] */
#define CMD_OUT      6  /* Set output, parameter : { 0 - set output to zero; 255 - set output to 1;
                         *                           else - output pulse with duration = 10ms * parameter} */
#define CMD_PROGRAM  5  /* Set device ID command, parameter : [0..255]   */
#define CMD_PROGPWM  60 /* Set initial PWM command, parameter : [PWM_MIN..PWM_MAX] */
#define CMD_STOP     9  /* Stop command. */
#define CMD_START   13  /* Start command. */

union {
    uint8_t arr[4];
    struct {
        uint8_t address;
        uint8_t command;
        uint8_t parameter;
        uint8_t checksum;
    } f;
} spi_data = {0};

uint8_t spi_idx = 0;

volatile uint8_t spi_ready = 0;


ISR (USI_OVF_vect)
{
    uint8_t byte = USIDR;
    
    if (spi_ready == 0) 
    {
        if (byte == START_BYTE) 
        {
            spi_idx = 0;
        }
        else 
        {
            spi_data.arr[spi_idx & 3] = byte;
            spi_idx += 1;
        }
        if (spi_idx >= 4) {
            if ((0xFF & (spi_data.f.address + spi_data.f.command
                    + spi_data.f.parameter)) == spi_data.f.checksum) 
            {
                spi_ready = 1;
            }
        }
    }
    
    USISR = 0b01000000;
}


/**
 * Write byte to EEPROM.
 * \param ucAddress address
 * \param ucData data
 */
void EEPROM_write(uint8_t ucAddress, uint8_t ucData)
{
    /* Wait for completion of previous write */
    while(EECR & (1<<EEPE))
    ;
    /* Set Programming mode */
    EECR = (0 << EEPM1) | (0 >> EEPM0);
    /* Set up address and data registers */
    EEARL = ucAddress;
    EEDR = ucData;
    /* Write logical one to EEMPE */
    EECR |= (1<<EEMPE);
    /* Start eeprom write by setting EEPE */
    EECR |= (1<<EEPE);
}

/**
 * Read byte from EEPROM.
 * \param ucAddress address
 * \return data
 */
uint8_t EEPROM_read(uint8_t ucAddress)
{
    /* Wait for completion of previous write */
    while(EECR & (1<<EEPE))
    ;
    /* Set up address register */
    EEARL = ucAddress;
    /* Start eeprom read by writing EERE */
    EECR |= (1<<EERE);
    /* Return data from data register */
    return EEDR;
}

uint8_t getDeviceId(void)
{
    if (EEPROM_read(EEPROM_MAGIC) == MAGIC)
    {
        return EEPROM_read(EEPROM_DEV_ID);
    }
    return 0;
}

uint8_t getInitialPWM(void)
{
    uint8_t duty = EEPROM_read(EEPROM_PWM_DUTY);
    if ((duty >= PWM_MIN) && (duty <= PWM_MAX))
    {
        return duty;
    }
    return PWM_MIN;
}

int main(void)
{
    uint8_t my_id = getDeviceId();
    
    /* configure pin directions */
    DDRB = (1 << SERVO_PIN) | (1 << SOLENOID_PIN) | (1 << MISO_PIN);
    PORTB = 0x0;
    /**/
    
    _delay_ms(1000.);
    
    /* configure PWM timer:
     *  - use OC1B signal as output
     *  - PWM mode
     *  - configure signal toggle value */
    
    TCCR1 = 0b00000110;
    GTCCR = 0b01100000;
    OCR1B = getInitialPWM();
    OCR1C = PWM_TIMER_RESET;
    
    cli();
    /* configure SPI slave */
    USICR = 0b01011000;
    /**/
    
    _delay_ms(1000.);
    
    sei();
    
    while (1)
    {
        
        if (spi_ready) 
        {
            if ((spi_data.f.address == 0xFF) || (spi_data.f.address == my_id))
            {
                switch (spi_data.f.command)
                {
                    case CMD_PWM_DUTY:
                        OCR1B = spi_data.f.parameter;
                        break;
                    case CMD_OUT:
                        if (spi_data.f.parameter == 0) {
                            PORTB &= ~(1 << SOLENOID_PIN);
                        }
                        else {
                            PORTB |= (1 << SOLENOID_PIN);
                            if (spi_data.f.parameter != 0xFF) {
                                uint8_t i;
                                for (i = 0; i < spi_data.f.parameter; ++i)
                                    _delay_ms(10.);
                                PORTB &= ~(1 << SOLENOID_PIN);
                            }
                        }
                        break;
                    case CMD_PROGRAM:
                        EEPROM_write(EEPROM_DEV_ID, spi_data.f.parameter);
                        EEPROM_write(EEPROM_MAGIC, MAGIC);
                        break;
                    case CMD_PROGPWM:
                        EEPROM_write(EEPROM_PWM_DUTY, spi_data.f.parameter);
                        break;
                    case CMD_STOP:
                        /* set all pins as inputs */
                        PORTB = 0;
                        DDRB = 0;
                        break;
                    case CMD_START:
                        DDRB = (1 << SERVO_PIN) | (1 << SOLENOID_PIN);
                        break;
                    default:
                        break;
                }
            }
            spi_ready = 0;
        }
        
#if 0
        _delay_ms(600.);
        
        if (value >= PWM_MAX)
            add = -1;
        else if (value <= PWM_MIN)
            add = 1;
        
        value += add;
        
        /*OCR0B = value & 0xFF;*/
        OCR1B = value & 0xFF;
#endif   
        
    }
}
