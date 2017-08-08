#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#define IR_RX_TIMEOUT 20000

// Port A Pins
#define RED_LED 0
#define YELLOW_LED 7
#define GREEN_LED 3
#define BLUE_LED 1

#define RED_LED_COMM 0
#define YELLOW_LED_COMM 2
#define GREEN_LED_COMM 3
#define BLUE_LED_COMM 1

#define BUTTON 2


// Port B Pins
#define IR_TX 0
#define IR_RX_POWER 1
#define IR_RX 2


uint8_t my_colors = 0;
uint16_t my_id = 0;
uint16_t red_source_id = 0;
uint16_t green_source_id = 0;
uint16_t blue_source_id = 0;
uint16_t yellow_source_id = 0;


void stop_timer(void)
{
    cli();
    TCCR1A = 0;
    TCCR1B = 0;
    TIMSK1 = 0;
    sei();
}

void EEPROM_write(uint8_t address, uint8_t data)
{
    /* Wait for completion of previous write */
    while(EECR & (1<<EEPE));
    /* Set Programming mode */
    EECR = (0<<EEPM1)|(0<<EEPM0);
    /* Set up address and data registers */
    EEAR = address;
    EEDR = data;
    /* Write logical one to EEMPE */
    EECR |= (1<<EEMPE);
    /* Start eeprom write by setting EEPE */
    EECR |= (1<<EEPE);
}

uint8_t EEPROM_read(uint8_t address) {
    /* Wait for completion of previous write */
    while(EECR & (1<<EEPE));
    /* Set up address register */
    EEAR = address;
    /* Start eeprom read by writing EERE */
    EECR |= (1<<EERE);
    /* Return data from data register */
    return EEDR;
}

void erase_state(void)
{
    for(uint8_t i = 0; i < 11; i++)
    {
        EEPROM_write(i, 0xFF);
    }
}

void save_state(void)
{
    EEPROM_write(0, my_colors);
    EEPROM_write(1, my_id>>8);
    EEPROM_write(2, my_id);
    EEPROM_write(3, red_source_id>>8);
    EEPROM_write(4, red_source_id);
    EEPROM_write(5, green_source_id>>8);
    EEPROM_write(6, green_source_id);
    EEPROM_write(7, blue_source_id>>8);
    EEPROM_write(8, blue_source_id);
    EEPROM_write(9, yellow_source_id>>8);
    EEPROM_write(10, yellow_source_id);
}

void restore_state(void)
{
    my_colors = EEPROM_read(0);
    my_id = (((uint16_t)EEPROM_read(1))<<8) + EEPROM_read(2);
    red_source_id = (((uint16_t)EEPROM_read(3))<<8) + EEPROM_read(4);
    green_source_id = (((uint16_t)EEPROM_read(5))<<8) + EEPROM_read(6);
    blue_source_id = (((uint16_t)EEPROM_read(7))<<8) + EEPROM_read(8);
    yellow_source_id = (((uint16_t)EEPROM_read(9))<<8) + EEPROM_read(10);
}

void delay(uint16_t ms)
{
    set_sleep_mode(SLEEP_MODE_IDLE);
    sei();
    for(uint16_t i = 0; i < ms/16; i++)
    {
        WDTCSR |= (1<<WDIE);
        sleep_enable();
        sleep_mode();
    }
    WDTCSR &= ~(1<<WDIE);
}

void delay_100us(uint16_t t)
{
    for (uint16_t i = 0; i < t; i++)
    {
        for(uint16_t j = 0; j < 91; j++)
        {
            __asm__ __volatile__ ("nop");
        }
    }
}

uint8_t roulette(uint8_t options)
{
    uint8_t led = (1<<BLUE_LED);
        
    //button might still be down from reset - wait for it to be released
    while(!(PINA & (1<<BUTTON)))
    {
        PORTA |= ((1<<BLUE_LED)|(1<<RED_LED)|(1<<YELLOW_LED)|(1<<GREEN_LED));
    }
    
    // display roulette pattern until button pressed
    while(PINA & (1<<BUTTON))
    {
        switch (led)
        {
            case (1<<BLUE_LED):
                led = (1<<RED_LED);
                break;
            case (1<<RED_LED):
                led = (1<<YELLOW_LED);
                break;
            case (1<<YELLOW_LED):
                led = (1<<GREEN_LED);
                break;
            case (1<<GREEN_LED):
                led = (1<<BLUE_LED);
                break;
        }
        if(led & options)
        {
            PORTA &= ~((1<<BLUE_LED)|(1<<RED_LED)|(1<<YELLOW_LED)|(1<<GREEN_LED));
            PORTA |= led;
            delay(50);
        }
    }
    // wait for button to be released and debounce before returning
    while(!(PINA & (1<<BUTTON)));
    delay(100);
    return led;
}

void ir_tx_bit(uint8_t bit)
{
    cli();
    TCNT1 = 0;
    TCCR1A = 0;
    TCCR1B = (1 << CS10);
    OCR1A = 102;
    TCCR1B |= (1<<WGM12);
    TIMSK1 = 1<<OCIE1A;
    sei();
    
    if(bit)
    {
        delay_100us(13);
        stop_timer();
        PORTB &= ~(1<<IR_TX);
        delay_100us(7);
    }
    else
    {
        delay_100us(7);
        stop_timer();
        PORTB &= ~(1<<IR_TX);
        delay_100us(13);
    }
}

void ir_wait_tx(uint16_t time)
{
    for(uint8_t i = 0; i < time; i++)
    {
        delay_100us(1);
        if(!(PINB & (1<<IR_RX)))
        {
            i = 0;
        }
    }
}

void ir_tx(uint32_t tx_data, uint8_t n_bits)
{
    for(uint8_t i = 0; i < n_bits; i++)
    {
        if(tx_data & 1)
        {
            ir_tx_bit(1);
        }
        else
        {
            ir_tx_bit(0);
        }
        tx_data = tx_data >> 1;
    }
}

uint32_t ir_rx(uint8_t n_bits)
{
    uint32_t rx_data = 0;
    uint8_t set_bit = 0;
    uint8_t count = 0;
    uint16_t timer = 0;
    
    
    for(uint8_t i = 0; i < n_bits; i++)
    {
        while((PINB & (1<<IR_RX)))
        {
            if (timer > IR_RX_TIMEOUT)
            {
                return 0;
            }
            delay_100us(1);
            timer++;
        }
        while(!(PINB & (1<<IR_RX)))
        {
            if(timer > IR_RX_TIMEOUT)
            {
                return 0;
            }
            delay_100us(1);
            count++;
            timer++;
        }
        if(count > 15) // measured 19 for long, 11 for short
        {
            rx_data |= (((uint32_t)1)<<set_bit);
        }
        set_bit += 1;
        count = 0;
    }
    return rx_data;
}

void angry_blink(uint8_t led)
{
    for(uint8_t i = 0; i < 5; i++)
    {
        PORTA &= ~((1<<BLUE_LED)|(1<<RED_LED)|(1<<YELLOW_LED)|(1<<GREEN_LED));
        delay(100);
        PORTA |= (1<<led);
        delay(100);
    }
}


void exchange_colors(void)
{
    // turn on IR receiver, make sure IR tramsitter is off
    PORTB |= (1<<IR_RX_POWER);
    PORTB &= ~(1<<IR_TX);

    uint32_t tx_buffer = 0;
    
    
    //pack the colors into 4 bits
    uint8_t my_colors_comm = 0;
    if(my_colors & (1<<YELLOW_LED))
    {
        my_colors_comm |= (1<<YELLOW_LED_COMM);
    }
    
    if(my_colors & (1<<RED_LED))
    {
        my_colors_comm |= (1<<RED_LED_COMM);
    }
    
    if(my_colors & (1<<GREEN_LED))
    {
        my_colors_comm |= (1<<GREEN_LED_COMM);
    }
    
    if(my_colors & (1<<BLUE_LED))
    {
        my_colors_comm |= (1<<BLUE_LED_COMM);
    }
    
    uint8_t my_checksum = ((my_colors_comm)^(my_id)^(my_id>>4)^(my_id>>8)^(my_id>>12)) & 0x0F;
    tx_buffer = my_colors_comm + (((uint32_t)my_id) << 4) + (((uint32_t)my_checksum) << 20);
    
    delay(200); //let receiver stabilize
    
    //wait for a few bit periods of silence
    ir_wait_tx(100);
    
    // transmit
    ir_tx(tx_buffer, 24);
    
    //receive with about .5s timeout
    uint32_t rx_buffer = ir_rx(24);
    
    //return if receive timed out
    if(rx_buffer == 0)
    {
        // turn off IR receiver and make sure IR tramsitter is off
        PORTB &= ~((1<<IR_RX_POWER)|(1<<IR_TX)) ;
        return;
    }
    
    delay(20);
    
    //transmit again
    ir_tx(tx_buffer, 24);
    
    // turn off IR receiver and make sure IR tramsitter is off
    PORTB &= ~((1<<IR_RX_POWER)|(1<<IR_TX)) ;
    
    uint8_t rx_colors_comm = (uint8_t) (rx_buffer & 0x0F);
    uint16_t rx_id = (uint16_t) ((rx_buffer >> 4) & 0xFFFF);
    uint8_t checksum_rx = ((rx_buffer >> 20) & 0x0F);
    uint8_t checksum_calculated = ((rx_colors_comm)^(rx_id)^(rx_id>>4)^(rx_id>>8)^(rx_id>>12)) & 0x0F;
    
    // abort if the data seems mangled
    if(checksum_rx != checksum_calculated)
    {
        return;
    }
    
    uint8_t rx_colors = 0;
    //unpack color data
    if(rx_colors_comm & (1<<YELLOW_LED_COMM))
    {
        rx_colors |= (1<<YELLOW_LED);
    }
    
    if(rx_colors_comm & (1<<RED_LED_COMM))
    {
        rx_colors |= (1<<RED_LED);
    }
    
    if(rx_colors_comm & (1<<GREEN_LED_COMM))
    {
        rx_colors |= (1<<GREEN_LED);
    }
    
    if(rx_colors_comm & (1<<BLUE_LED_COMM))
    {
        rx_colors |= (1<<BLUE_LED);
    }
    
    // check to see if we've already received a color from this device
    if(rx_id == red_source_id)
    {
        angry_blink(RED_LED);
        return;
    }
    if(rx_id == green_source_id)
    {
        angry_blink(GREEN_LED);
        return;
    }
    if(rx_id == blue_source_id)
    {
        angry_blink(BLUE_LED);
        return;
    }
    if(rx_id == yellow_source_id)
    {
        angry_blink(YELLOW_LED);
        return;
    }
    
    rx_colors &= ~my_colors; // keep only the colors we don't already have
    
    // if we already have all of the colors they have, return
    if (rx_colors == 0)
    {
        return;
    }
    
    uint8_t new_color = 0;
    // if there's only one new one, go ahead and assign it
    if (rx_colors == (1<<RED_LED) || rx_colors == (1<<GREEN_LED) || rx_colors == (1<<YELLOW_LED) || rx_colors == (1<<BLUE_LED))
    {
        new_color |= rx_colors;
    }
    else
    { // if we received multiple colors we don't have, let the user choose
        new_color = roulette(rx_colors);
    }
    my_colors |= new_color;
    
    // record the sender of this new color
    switch(new_color)
    {
        case (1<<RED_LED):
            red_source_id = rx_id;
            break;
        case (1<<GREEN_LED):
            green_source_id = rx_id;
            break;
        case (1<<BLUE_LED):
            blue_source_id = rx_id;
            break;
        case (1<<YELLOW_LED):
            yellow_source_id = rx_id;
            break;
    }
}


int main(void)
{
    // Set output pins to output mode
    DDRA |= (1<<RED_LED)|(1<<YELLOW_LED)|(1<<GREEN_LED)|(1<<BLUE_LED);
    DDRB |= (1<<IR_RX_POWER)|(1<<IR_TX);

    //Enable pullup on button
    PORTA |= (1<<BUTTON);
    
    ADCSRA &= ~(1<<ADEN); //draws significant current in sleep mode if not disabled
    
    // Check if this is the first run (blank EEPROM will read 0xFF)
    my_colors = EEPROM_read(0);
    if(my_colors == 0xFF) // it's the first run
    {
        //start the timer with no interrupts
        cli();
        TCCR1A = 0;
        TCCR1B = (1 << CS10);
        TIMSK1 = 0;
        sei();
        
        // Have user choose an inital color
        my_colors = roulette((1<<RED_LED)|(1<<YELLOW_LED)|(1<<GREEN_LED)|(1<<BLUE_LED));
        
        // stop timer to get a random 16 bit id
        stop_timer();
        my_id = TCNT1;
        if(my_id == 0)
            my_id = 1;
        
        save_state();
    }
    else // it's not the first run
    {
        restore_state();
    }
    
    uint16_t button_down_time = 0;
    
    // main loop
    while(1)
    {
        PORTA &= ~((1<<BLUE_LED)|(1<<RED_LED)|(1<<YELLOW_LED)|(1<<GREEN_LED));
        PORTA |= (((1<<BLUE_LED)|(1<<RED_LED)|(1<<YELLOW_LED)|(1<<GREEN_LED)) & my_colors);
        
        // things that happen on button release
        if((PINA & (1<<BUTTON)) && (button_down_time != 0))
        {
            //exchange colors after a short press
            if (button_down_time < 50)
            {
                exchange_colors();
                save_state();
                button_down_time = 0;
            }
            
            // actually sleep after a long press is released
            if(button_down_time >= 50)
            {
                //debounce
                PORTA &= ~((1<<BLUE_LED)|(1<<RED_LED)|(1<<YELLOW_LED)|(1<<GREEN_LED));
                delay(200);
                GIMSK |= (1 << PCIE0);
                PCMSK0 |= (1 << PCINT2);
                sei();
                set_sleep_mode(SLEEP_MODE_PWR_DOWN);
                sleep_enable();
                sleep_mode();
                // pin change interrupt on button enabled - short press wakes
                sleep_disable();
                GIMSK &= ~(1 << PCIE0);
                PCMSK0 &= ~(1 << PCINT2);
                button_down_time = 0;
                
                PORTA |= (((1<<BLUE_LED)|(1<<RED_LED)|(1<<YELLOW_LED)|(1<<GREEN_LED)) & my_colors);
                // wait for button release and debounce
                while(!(PINA & (1<<BUTTON)));
                delay(200);
            }
        }
        
        // things that happen while button is held down
        if(!(PINA & (1<<BUTTON)))
        {
            button_down_time++;
            
            // appear to sleep after a long press
            if(button_down_time >= 50)
            {
                PORTA &= ~((1<<BLUE_LED)|(1<<RED_LED)|(1<<YELLOW_LED)|(1<<GREEN_LED));
            }
            
            // reset after a very long press
            if(button_down_time > 500)
            {
                button_down_time = 0;
                my_colors = 0;
                red_source_id = 0;
                green_source_id = 0;
                blue_source_id = 0;
                yellow_source_id = 0;
                erase_state();
                
                cli();
                TCCR1A = 0;
                TCCR1B = (1 << CS10);
                TIMSK1 = 0;
                sei();
                
                my_colors = roulette((1<<RED_LED)|(1<<YELLOW_LED)|(1<<GREEN_LED)|(1<<BLUE_LED));
                
                stop_timer();
                my_id = TCNT1;
                if(my_id == 0)
                    my_id = 1;
                
                save_state();
            }
            
        }
        
        delay(16);
    }
}

ISR(TIM1_COMPA_vect)
{
    PORTB ^= (1<<IR_TX);
}

ISR(WDT_vect) {
    sleep_disable();
}

ISR(PCINT0_vect) {
    sleep_disable();
}

ISR(PCINT1_vect) {
    sleep_disable();
}
