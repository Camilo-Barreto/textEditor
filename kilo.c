/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

// Function to print errors
void die(const char *s) {
	// Clear the screen on exit
	// 1. Using ANSI escape code to tell the terminal to clear the screen
	// Writing 4 bytes to the terminal
	// \x1b (an escape character or 27 in decimal)
	// The other three are [2J 
	write(STDOUT_FILENO, "\x1b[2J", 4);
	
	// 2. The cursor stays at the same positions
	// So, the following line moves it to the top left
	// Writing 3 bytes, the escape char, [ and H
	// where H is the cursor position
	write(STDOUT_FILENO, "\x1b[H", 3);
	
	perror(s);
	exit(1);
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
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
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

// waits for a keypress, reads and returns it
char editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	return c;
}

/*** output ***/

// draw tildes on the left
void editorDrawRows() {
	int y;
	
	// Not sure of terminal size so drawing 24 tildes for now
	for (y = 0; y < 24; y++) {
		// draw the tildes and the new line escape char \r\n
		write(STDOUT_FILENO, "~\r\n", 3);
	}
}

void editorRefreshScreen() {
	// Using ANSI escape code to tell the terminal to clear the screen
	// Writing 4 bytes to the terminal
	// \x1b (an escape character or 27 in decimal)
	// The other three are [2J 
	write(STDOUT_FILENO, "\x1b[2J", 4);
	
	// The cursor stays at the same positions
	// So, the following line moves it to the top left
	// Writing 3 bytes, the escape char, [ and H
	// where H is the cursor position
	write(STDOUT_FILENO, "\x1b[H", 3);
	
	// draw tildes
	editorDrawRows();
	
	// move cursor to the top left
	write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** input ***/

void editorProcessKeypress() {
	char c = editorReadKey();

	switch (c) {
		// CTRL-Q will exit the program
		case CTRL_KEY('q'):
			// Clear the screen
			write(STDOUT_FILENO, "\x1b[2J", 4);
			
			// Move cursor to the top left
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
	}
}

/*** init ***/

int main() {
	enableRawMode();

	while (1) {
		// Clear the screen
		editorRefreshScreen();
		// Process the keypresses
		editorProcessKeypress();
	}

	return 0;
}
