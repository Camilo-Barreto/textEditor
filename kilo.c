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
	// IXON - this will disable ctrl-S and ctrl-Q which pauses read and write
	// ICRLN will disable ctrl-M
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	// all post processing output will be deleted
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	// ISIG will disable ctrl-C and ctrl-Z
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

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
			// \r\n will solve the problem where after each char, the cursor doesnt move back to the left but is diagonal; fixes carriage returns
			printf("%d\r\n", c);
		}
		else {
			printf("%d ('%c')\r\n", c, c);
		}
	}

	return 0;
}
