# Stucco

## About the project

Stepper control via Arduino via serial.

An external Bluetooth serial adapter (like the HM-10) can be connected to D12/D2 (RX/TX).

I've used the 28BYJ-48 stepper motor with the ULN2003 driver connected to pins 8 to 11.

## Using the controller

You can issue commands to the module. Commands are either terminated by newline or the semicolon (`;`)
character. Some commands accept parameters, these are separated by a space character, currently parameters
cannot have space character in them.

Move commands are queued and are their execution is started immediately. You may freeze the execution, so
you can enter the commands in advance.

## Commands

- `status`: lists the command queue
- `right <amount> [time]`/`left <amount> [time]`: rotate the stepper motor clockwise/counterclockwise
  by the given amount (either the number of steps, or the angle in degrees with the suffix `d` added
  without space, e.g. `360d` for a full turn) in the given amount of time given in seconds (or as fast
  as possible if omitted)

## Applications

Timelapse camera rotation, star tracker motor control.
