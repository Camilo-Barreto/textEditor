/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

// Using enum for arrow keys
// left is 1000, following constants are incremently set]
// right is 1001, up is 1002, down is 1003
enum editorKey {
	ARROW_LEFT = 1000,
  	ARROW_RIGHT,
  	ARROW_UP,
  	ARROW_DOWN,
	PAGE_UP,
	PAGE_DOWN
};

/*** data ***/

// global struct
struct editorConfig {
	// cx and cy will keep track of the cursor position
	int cx, cy;
	int screenrows;
	int screencols;
	struct termios orig_termios;
};

struct editorConfig E;

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
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
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
int editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	
	// if an escape key is read we immediately read 2 more bytes into the seq buffer.
	if (c == '\x1b') {
		// seq buffer is 3 bytes long because we will be handling longer escape sequences in the future
		char seq[3];
		
		// if either of these reads time out (after 0.1 secs) we can assume the user pressed an esc key and therefore we return it
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		// if the esc sequence is an arrow key esc sequence, return the corresponding wasd character
		if (seq[0] == '[') {

			// if the byte after [ is a digit we test if the 2nd char is ~. Then we test if the number before it was a 5 or a 6
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
					}
				}
			}
			else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
				}
			}
		}
		// if the esc char is not recognised return the escape character
		return '\x1b';
	}
	else {
		return c;
	}
}

int getCursorPosition(int *rows, int *cols) {
	char buf[32];
	unsigned int i = 0;

	// the n command is used to query the status information
	// 6 asks for the cursor position
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;

		if (buf[i] == 'R') break;
		i++;
	}

	buf[i] = '\0';
	
	// First, it checks if the buf responds with an esc sequence
	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	// Passes a pointer to the third character of buf, this is done to skip the '\x1b' and '[' chars.
	// %d;%d is passed to tell it to parse the two integers and save the values to rows and cols variables.
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		// If the previous method failed, use this one 
		// This method places cursor on the bottom right to find the screen dimensions
		// Done by using 2 esc chars to send cursor to the right (C) then to the bottom (B)
		// The 999 tells by how much to move the cursor so using a large no it can be achieved
		// The C and B keeps the curson within the screen as stated in the documentation
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		editorReadKey();
		return getCursorPosition(rows, cols);
	}
	else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}
/*** append buffer ***/

struct abuf {
	// abuf is short for append buffer
	// pointer for the buffer and the length
	char *b;
	int len;
};

// Macro to initialize an abuf instance with a NULL pointer and length 0
#define ABUF_INIT {NULL, 0};


// Function to append a string 's' of length 'len' to the buffer 'ab'
void abAppend(struct abuf *ab, const char *s, int len) {
	// This will reallocate the buffer to accomodate the new string
	char *new = realloc(ab->b, ab->len + len);
	
	// If reallocation fails return without making changes
	if (new == NULL) return;

	// Reallocation was successfull. Copies the string s into the reallocated buffer
	memcpy(&new[ab->len], s, len);
	// Updates buffer pointer to newly reallocated memory
	ab->b = new;
	// Update the buffer length to include the length of the new string
	ab->len += len;
}

// Function to free the memory allocated for ab
void abFree(struct abuf *ab) {
	free(ab->b);
}
/*** output ***/

// draw tildes on the left
void editorDrawRows(struct abuf *ab) {
	int y;
	
	// Draw tildes for the entire screen
	for (y = 0; y < E.screenrows; y++) {
		if (y == E.screenrows / 3) {
			char welcome[80];

			// Using a safe printf function to store welcome message
			int welcomelen = snprintf(welcome, sizeof(welcome),
					"Kilo editor -- version %s", KILO_VERSION);

			if (welcomelen > E.screencols) welcomelen = E.screencols;
			
			// Calculating the middle of the line
			int padding = (E.screencols - welcomelen) / 2;
			if (padding) {
				abAppend(ab, "~", 1);
				// Length of padding is decreased as tilde is drawn
				padding--;
			}
			// Adding spaces until the end of padding to print the welcome message
			while (padding--) abAppend(ab, " ", 1);
			// Add the welcome message to the ab buffer to display the message
			abAppend(ab, welcome, welcomelen);
		} 
		else {
			// draw the tildes on every line
			abAppend(ab, "~", 1);
		}

		// Instead of clearing the entire screen at once, clear it line by line
		// Using ANSI escape code to tell the terminal to clear the screen
		// Writing 3 bytes to the terminal
		// \x1b (an escape character or 27 in decimal)
		// The other two are [K (Note: [2J clears the entire screen at once)
		abAppend(ab, "\x1b[K", 3);
		
		// Use an escape char until the second last line except the last one to fix the issue where the last line wont display the tilde.
		if (y < E.screenrows - 1) {
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;

	// Hide the cursor before refreshing the screen
	abAppend(&ab, "\x1b[?25l", 6);
	
	// The cursor stays at the same positions
	// So, the following line moves it to the top left
	// Writing 3 bytes, the escape char, [ and H
	// where H is the cursor position
	abAppend(&ab, "\x1b[H", 3);
	
	// draw tildes
	editorDrawRows(&ab);
	
	// move cursor to x=1 and y=1
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	abAppend(&ab, buf, strlen(buf));

	// Show the cursor that was hidden before refreshing the screen
	abAppend(&ab, "\x1b[?25h", 6);
	
	// Using a single write instead of previous 3 writes to write the buffer to the screen for the lenght of the buffer
	write(STDOUT_FILENO, ab.b, ab.len);
	// Freeing up memory used by abuf
	abFree(&ab);
}

/*** input ***/

// Function to process awsd keys to move the cursor
// Function called in editorProcessKeypress()
// Cursor cannot move oustside screen
void editorMoveCursor(int key) {
	switch (key) {
	case ARROW_LEFT:
		if (E.cx != 0) {
			E.cx--;
		}
		break;
	case ARROW_RIGHT:
		if (E.cx != E.screencols - 1) {	
			E.cx++;
		}
		break;
	case ARROW_UP:
		if (E.cy != 0) {
			E.cy--;
		}
		break;
	case ARROW_DOWN:
		if (E.cy != E.screenrows - 1) {
			E.cy++;
		}
		break;
	}
}

void editorProcessKeypress() {
	int c = editorReadKey();

	switch (c) {
		// CTRL-Q will exit the program
		case CTRL_KEY('q'):
			// Clear the screen
			write(STDOUT_FILENO, "\x1b[2J", 4);
			
			// Move cursor to the top left
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			// The following code block declares the time variable. To move the cursor we simulate the user pressing the up and down keys multiple time to send the cursor to the bottom or the top of the page. 
			{
				int times = E.screenrows;
				while (times--) {
					editorMoveCursor(c == PAGE_UP ? ARROW_UP: ARROW_DOWN);
				}
			}
			break;
		
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
	}
}

/*** init ***/

// set window size
void initEditor() {
	// Initialising cursor position
	E.cx = 0;
	E.cy = 0; 
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
	enableRawMode();
	initEditor();

	while (1) {
		// Clear the screen
		editorRefreshScreen();
		// Process the keypresses
		editorProcessKeypress();
	}

	return 0;
}
