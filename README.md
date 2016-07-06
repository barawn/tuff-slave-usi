# tuff-slave-usi

Firmware for the TUFF slave with USI interface.

# TUFF Command Protocol

All commands are 16 bits. Clock idles high, and data is latched on the rising edge of clock. This corresponds to SPI mode 3.

## Global Commands

Global commands are identified by the most significant bit being set.

* 0xFFFF:  Reset. Places microcontroller back into “locked” state.
* 0xD00D: Releases microcontroller from “locked” state, and makes it ready to accept commands.

In the ‘locked’ state, any commands other than 0xD00D produce a 1 millisecond delay. Synchronizing TUFFs can therefore
be done by putting the TUFF in a locked state (send 0xFFFF twice) and then sending 0xD00D. Alternatively, RESET can be toggled, 
and then 0xD00D can be sent.

## Normal Commands

Normal commands have an address byte (the most significant byte) and a command byte (the least significant byte).

The 6 least significant bits (mask=0x3F) of the address byte are a bitmask of the addressed microcontrollers (channels). The 6th bit (bit 0x40) indicates which TUFF in a stack (0=lower, 1=upper) is being addressed.
A high bit being set indicates that it’s a global command.

Commands are decoded by looking at the 3 most significant bits of the command byte (mask = 0xE0).

* 0x00: Update cap state for cap 0.
* 0x20: Update cap state for cap 1.
* 0x40: Update cap state for cap 2.
* 0x60: Save current cap states to default.
* 0x80/0xC0: Update notch state.

0xA0/0xE0 function as ‘update notch state’ as well, but bit 5 is used as information: so the notch state update should be indicated by just setting 0x80.

### Update Cap State

The Update Cap State commands (0x00/0x20/0x40) use the least significant 5 bits in the command byte for the setting. 
That is, a command byte of 0x5F sets cap 2 to 31, because in binary this is 0101_1111, and the low 5 bits (11111) equal 31.

In C this would be ((cap & 0x3) << 5) | (value & 0x1F)).

### Save Default

The Save Default command (0x60) saves the current cap states as their default settings. Note that the notches are always OFF
by default. This just sets the default setting of the capacitor.

### Update Notch State

The update notch state command uses the low 6 bits as a notch mask and a notch state indicator. Bits 3,4,5 determine which notches 
are changed by the command, and Bits 0,1,2 determine the state of those notches.

So, for instance, to turn ON notch 0 and OFF notch 2, it would be 0xA9, since that is 1010_1001 in binary. Splitting this up, 
the top 2 bits indicate the command (10), the next 3 bits are the notches to change (101), and the last 3 bits are the new notch
states (001).

In C, this would be (0x80 | (notches_to_change << 3) | (new_notch_states));

notches_to_change here is a 3-bit bitmask of the notches that you want to change. If a bit is 1, change that notch, 
if a bit is 0, do not change it.

new_notch_states here are the states of the 3 notches. If the corresponding bit in notches_to_change is 0, that bit is a
don’t care.
