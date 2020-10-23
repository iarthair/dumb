# Dumb

## The world's dumbest dumb terminal.

I wrote this while developing an ARM based product for a company in England
about 17 years ago.  I needed to talk to the target on an RS-232 connection and
download a bootstrap program using the [XMODEM][] protocol (this was dictated
by the processor firmware when it did not find a bootloader on first power on).
Use of XMODEM was and is pretty common in embedded environments, partly for its
simplicity.

Try as I might I could not get any of the usual terminal emulators with XMODEM
support to work. I figured that since the various Unix/Linux terminal programs
already did the terminal emulation part, what I really needed was a Telnet/SSH
style program that worked over a tty.  The `cu` program that is part of UUCP
would have been about right, except that there is no XMODEM capability, so I
wrote this.  Unlike `cu` it does not fork an extra process.

Even now for most embedded system downloads, it has proven more reliable than
the more sophisticated terminal programs, across a range of target
microcontrollers.

## Build

Dumb should compile without problem on any system with `poll()` and `termios`.
There is also support for GNU readline.  To compile (it's hardly worth even
providing a build system for this):

```sh
$ gcc dumb.c -lreadline -o dumb
```

or without readline support

```sh
$ gcc -DNOREADLINE dumb.c -o dumb
```

and copy the result to `$HOME/bin` or wherever you see fit!

## Usage

```sh
$ dumb [options]
```

### Options

* `-t` --- prepend responses from target system with a timestamp.
* `-h file` --- set the history file name for GNU readline (default: .dumb).
* `-l tty` --- set the tty for the target system (default: /dev/ttyS0).
* `-b baud` --- set the baudrate (default: 115200).
* `-e ch` --- set the escape character; specify a numeric value or `^x`
  notation (default `^A`).

Dumb copies all input to the remote and all output from the remote is echoed to
the terminal.  When the escape character is entered, dumb breaks out of normal
operation and executes a command depending on the next character as follows:

* `^A` (or current escape character) --- send character to remote.
* `t`, `T` --- toggle timestamp in prompt.
* `c`, `C` --- switch to command mode.
* `s`, `S` --- XMODEM send
* `h`, `H` --- browse history
* `x`, `X` --- exit dumb.

## Command Mode

Switching to command mode `^Ac` issues a prompt and reads a single command from
the console and executes it using `sh -c`. The command is added to the history
file.

## XMODEM Send

Switching to XMODEM send mode `^As` issues a prompt and reads a filename. The
filename is added to the history file.  Dumb then starts the XMODEM transfer to
the target system.  It displays a simple spinner and percentage complete during
the transfer.

[XMODEM]: https://en.wikipedia.org/wiki/XMODEM
