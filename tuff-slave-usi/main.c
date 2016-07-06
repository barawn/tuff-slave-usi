#include <msp430.h> 

// INCREMENT THIS FOR EACH TUFF:
// LowerTUFF: 0x01,0x02,0x04,0x08,0x10,0x20.
// UpperTUFF: 0x41,0x42,0x44,0x48,0x50,0x60
#pragma DATA_SECTION(my_address, ".infoD")
#pragma RETAIN(my_address)
const unsigned char my_address = 0x01;

/*
 * Primary TUFF code.
 *
 * Switches turn on when you have J1->J2 being insertion loss: that's V1 = 0, V2 = 3.3V.
 * V1 is pin 4 (RF1_SW0_ON). V2 is open, so it needs to be 3.3V?
 *
 *
 * Pinout:
 * P1.0: SW2_ON
 * P1.1: SW2_EN
 * P1.2: SW1_EN
 * P1.3: D
 * P1.5: CTRL_CLK (SMCLK)
 * P1.4: SW0_EN
 * P1.6: C
 * P1.7: CTRL_DATA (SDI/SDA)
 * P2.6: SW0_ON
 * P2.7: SW1_ON
 *
 *
 * There are 3 switches, so we need 2 bits to select them. We need 5 bits to control
 * the settings, so that leaves 1 bit for on/off.
 *
 * We'll try dealing with reprogramming defaults later.
 */

// InfoB contains the default switch values to be loaded into the capacitor.
#pragma DATA_SECTION(switch_defaults, ".infoB")
const unsigned char switch_defaults[3] = { 0x00, 0x00, 0x00 };
// switch_values contains the current values loaded into the capacitor.
#pragma NOINIT(switch_values)
unsigned char switch_values[3];

// InfoC contains the pinout information.
#pragma DATA_SECTION(notch_bits, ".infoC")
const unsigned char notch_bits[3] = { BIT6, BIT7, BIT0 };
#pragma DATA_SECTION(notch_ports, ".infoC")
volatile unsigned char *const notch_ports[3] = { &P2OUT, &P2OUT, &P1OUT };
#pragma DATA_SECTION(notch_en_bits, ".infoC")
const unsigned char notch_en_bits[3] = { BIT4, BIT2, BIT1 };

#define RF_C BIT6
#define RF_D BIT3

// Commanding consists of 16 bits clocked in.
// Right after startup, the device is 'locked', waiting to synchronize.
// After it receives 16 bits, it wakes up, checks to see if it matches
// 0xD00D, and if it does, it enters "unlocked" state.
// If it does *not*, then it sleeps for 100 milliseconds.
//
// After it is in an "unlocked" state, if it ever receives a 0xFFFF
// command, it goes back into a "locked" state (this is actually a 'reset').
//
// So to synchronize at power-on, the controller can just do:
// send 0xFFFF
// Sleep 100 ms. At this point, no matter what, the commmand buffer has only '1's in it.
// send 0xFFFF
// Sleep 100 ms. At this point, no matter what, the system is in a locked state.
// send 0xD00D
// Sleep 200 ms. At this point, either the system enters 'unlocked' state, or it sleeps and ignores the remaining clocks.
// send 0xD00D
// At this point, the system is unlocked and synchronized.
//
// 16-bit commands.
// Top bit, if set, indicate a special.
// 0xFFFF = reset
// 0xD00D = unlock
// (all others ignored)
// Next bit is a board indicator. Top boards get '1', bottom boards get '0'.
// Next 6 bits are a bitmask of addressed microcontrollers.
// Bottom 8 bits:
// 0yy x xxxx for y=0,1,2: set cap to x xxxx.
// 011 . .... update cap defaults to current settings
// 1.  xxxyyy if X set, turn on or off y

// All microcontrollers on a TUFF can be synchronized/commanded simultaneously.
//
// so to turn all notches on:
// 0xFFFF
// 0xFFFF
// 0xD00D
// 0x3FFF
// (or 0x3FBF,  the 6th bit doesn't matter)

#pragma NOINIT(ready)
volatile unsigned char ready;
#pragma NOINIT(cmd)
volatile unsigned int cmd;
#pragma NOINIT(unlocked)
unsigned char unlocked;
#pragma NOINIT(last_cmd);
unsigned int last_cmd;

const unsigned char p1dir_default = BIT0 | BIT1 | BIT2 | BIT3 | BIT4 | BIT6;
const unsigned char p2dir_default = BIT6 | BIT7;
const unsigned char p1out_default = BIT0 | BIT5;
const unsigned char p2out_default = BIT6 | BIT7;

// WORLD'S MOST INSANE CAST
// CCS somehow can't tell that my_address might change from build to build.
volatile const unsigned char *const addr_ptr = &my_address;

void update_switch(unsigned int sw, unsigned char val);
void reset_switches();
void set_notch_state(unsigned char ctrl);
void test_sequence();
void reset_all();

void reset_all() {
	P1OUT = p1out_default;
	P2OUT = p2out_default;
	P1DIR = p1dir_default;
	P2DIR = p2dir_default;
	reset_switches();
}

void delay_ms() {
	__delay_cycles(65535);
}

void delay_sec() {
	unsigned int i;
	for (i=0;i<100;i=i+1) {
		delay_ms();
	}
}

void test_sequence() {
	unsigned int i;
	delay_sec();
	update_switch(0, 0);
	set_notch_state(0x09);
	delay_sec();
	update_switch(0, 31);
	delay_sec();
	set_notch_state(0x08);
	delay_sec();
	update_switch(1, 0);
	set_notch_state(0x12);
	delay_sec();
	update_switch(1, 31);
	delay_sec();
	set_notch_state(0x10);
	delay_sec();
	update_switch(2, 0);
	set_notch_state(0x24);
	delay_sec();
	update_switch(2, 31);
	delay_sec();
	P1OUT |= BIT7;
	P1REN |= BIT7;
	delay_sec();
	for (i=0x1;i != 0x40;i=i<<1) {
		if (my_address & i) {
			P1OUT &= ~BIT7;
			P1DIR |= BIT7;
		}
		delay_sec();
		if (my_address & i) {
			P1DIR &= ~BIT7;
			P1OUT |= BIT7;
		}
		delay_sec();
	}
	P1REN &= ~BIT7;
	reset_all();
}

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;	// Stop watchdog timer
    BCSCTL1 = CALBC1_8MHZ;
    DCOCTL = CALDCO_8MHZ;
    // Switch P2 bit 6 bit 7 back to digital I/O.
    P2SEL &= ~(BIT6 | BIT7);
    // Pullup clock.
    P1OUT |= BIT5;
    P1REN |= BIT5;
    // initialize switches to *off*.
    reset_all();

    delay_ms();
    // Check to see if clock is low (to go into test sequence)
    if (!(P1IN & BIT5)) {
    	// Clock is low! Someone wants us to do a test sequence.
    	test_sequence();
    }

    ready = 0;
    cmd = 0;
    last_cmd = 0;
    unlocked = 0;

    // Enable SDI, enable CLK, MSB first, slave mode. Leave in soft reset.
    USICTL0 = USIPE7 | USIPE5 | USISWRST;
    // Hand pins over to USI
    P1SEL |= BIT5 | BIT7;
    // Clock is active low.
    USICKCTL = USICKPL;
    // Enable interrupt.
    USICTL1 = USIIE;
    // 16 bit mode.
    USICNT = USI16B;
    // Out of soft reset...
    USICTL0 &= ~USISWRST;
    // and ask to receive 16 bits.
    USICNT = USI16B | 16;
    __enable_interrupt();
    while(1) {
    	if (ready) {
    		__disable_interrupt();
    		ready = 0;
    		if (!unlocked) {
    			if (cmd == 0xD00D) {
    				unlocked = 1;
    			} else {
    				// Sleep for a while.
    				delay_ms();
    			}
    			// The USI is still in reset here, so no need to do anything.
    		} else {
    			unsigned char address;
    			if (cmd == 0xFFFF) {
    				unlocked = 0;
    			    P1OUT = p1out_default;
    			    P2OUT = p2out_default;
    			    reset_switches();
    			} else if (cmd & 0x8000) {
    				// ignore
    			} else {
        			address = (cmd & 0xFF00) >> 8;
        			cmd = cmd & 0xFF;
    				address = address & 0x7F;
    				if (address & *addr_ptr) {
    					unsigned char channel;
						unsigned char setting;

						// The subcmd is in the high bits (mask of 0xE0)
						// subcmd = 00, 20, 40 update cap 0, 1, 2
						// subcmd = 60 updates defaults.
						// subcmd = 80 sets the switch state.
						// Bits 3,4,5 determine which notches are being controlled.
						// Bits 0,1,2 determine setting of the controlled notches.
						if (cmd & 0x80) {
							set_notch_state(cmd);
						} else {
							// subcmd = 0x60 updates the defaults in flash to the current values.
							if ((cmd & 0x60) == 0x60) {
								update_defaults();
							} else {
								// subcmd 00, 20, 40 update caps 0, 1, 2.
								// Value is in the low 5 bits.
								setting = cmd & 0x1F;
								// Channel is in bits 5,6. So if we shift them
								// up 3, that puts them in bits 8, 9.
								cmd = cmd << 3;
								// and now we can snag them off of the top byte.
								channel = (cmd & 0xFF00) >> 8;
								// and bitmask it.
								channel = channel & 0x3;
								switch_values[channel] = setting;
								update_switch(channel, setting);
							}
						}
					}
    			}
    		}
    		last_cmd = cmd;
    		cmd = 0;
    		ready = 0;
    		// kick USI out of soft reset.
    		USICTL0 &= ~USISWRST;
    	    USICNT = USI16B | 16;
    	    USICTL1 |= USIIE;
    	}
    	// Enter LPM4 and sleep.
    	__low_power_mode_4();
    }
    return 0;
}

// Bits 3,4,5 are mask of notch states to set.
// Bits 0,1,2 are states to set them into.
// So to turn on notch 0, you would do
// 0x09 (bit 3 + bit 0)
// to turn on notch 1 and turn off notch 0, you would do
// 0x1A (bit 4 + bit 3 + bit 1)
void set_notch_state(unsigned char ctrl) {
	// notch state update
	if (ctrl & 0x08) {
		// notch 0
		if (ctrl & 0x01) {
			*notch_ports[0] &= ~notch_bits[0];
		} else {
			*notch_ports[0] |= notch_bits[0];
		}
	}
	if (ctrl & 0x10) {
		if (ctrl & 0x02) {
			*notch_ports[1] &= ~notch_bits[1];
		} else {
			*notch_ports[1] |= notch_bits[1];
		}
	}
	if (ctrl & 0x20) {
		if (ctrl & 0x04) {
			*notch_ports[2] &= ~notch_bits[2];
		} else {
			*notch_ports[2] |= notch_bits[2];
		}
	}
}


void reset_switches() {
    switch_values[0] = switch_defaults[0];
    switch_values[1] = switch_defaults[1];
    switch_values[2] = switch_defaults[2];
    update_switch(0, switch_values[0]);
    update_switch(1, switch_values[1]);
    update_switch(2, switch_values[2]);
}

void update_defaults() {
	FCTL2 = FWKEY + FSSEL_1 + FN4 + FN2 + FN0;
	FCTL3 = FWKEY;                       // Clear Lock bit
	FCTL1 = FWKEY + ERASE;               // Set Erase bit
	*((unsigned char *) switch_defaults) = 0;
	FCTL1 = FWKEY + WRT;
	*((unsigned char *) &switch_defaults[0]) = switch_values[0];
	*((unsigned char *) &switch_defaults[1]) = switch_values[1];
	*((unsigned char *) &switch_defaults[2]) = switch_values[2];
	FCTL1 = FWKEY;
	FCTL3 = FWKEY + LOCK;
}

void update_switch(unsigned int sw, unsigned char val) {
	unsigned int i;
	P1OUT |= notch_en_bits[sw];
	// We need to clock in 8 bits. Top 3 bits are all 0.
	for (i=8;i;i--) {
		if (val & 0x80) P1OUT |= RF_D;
		else P1OUT &= ~RF_D;
		P1OUT &= ~RF_C;
		P1OUT |= RF_C;
		val = val << 1;
		P1OUT &= ~RF_C;
	}
	// Lower SEN to activate new setting.
	P1OUT &= ~notch_en_bits[sw];
}

#pragma vector=USI_VECTOR
__interrupt void USI_ISR() {
	// Grab the command.
	cmd = USISR;
	ready = 1;
	// Put us in reset.
	USICTL0 |= USISWRST;
	// Kill the interrupt flag.
	USICTL1 &= ~USIIE;
	// And wake up.
	__bic_SR_register_on_exit(LPM4_bits);
}
