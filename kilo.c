#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode() {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disableRawMode);

	struct termios raw = orig_termios;

	raw.c_lflag &= ~(ECHO | ICANON);

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
	enableRawMode();

	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
		// Display the ascii of the key pressed but if 
		// it is a printable character then also print the char
		// iscntrl tests if the char is a control char.
		// Ctrl chars are 0-31 and 127.
		// ASCII codes 32-126 are all printable chars.
		if (iscntrl(c)) {
			printf("%d\n", c);
		}
		else {
			printf("%d ('%c')\n", c, c);
		}
	}

	return 0;
}
