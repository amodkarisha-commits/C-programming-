/*
 ██████╗ ███╗   ██╗ █████╗ ██╗  ██╗███████╗
██╔════╝ ████╗  ██║██╔══██╗██║ ██╔╝██╔════╝
╚═════╗  ██╔██╗ ██║███████║█████╔╝ █████╗
     ██║ ██║╚██╗██║██╔══██║██╔═██╗ ██╔══╝
██████╔╝ ██║ ╚████║██║  ██║██║  ██╗███████╗
╚═════╝  ╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝

  Console Snake Game — No external libraries needed
  Compile:
    Windows : g++ -o snake snake.cpp
    Linux   : g++ -o snake snake.cpp
    macOS   : g++ -o snake snake.cpp
  Run:/
    Windows : snake.exe
    Linux   : ./snake
    macOS   : ./snake
*/

#ifdef _WIN32
  #include <windows.h>
  #include <conio.h>
#else
  #include <termios.h>
  #include <unistd.h>
  #include <sys/select.h>
  #include <sys/ioctl.h>
#endif

#include <iostream>
#include <vector>
#include <deque>
#include <string>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>

using namespace std;
using namespace std::chrono;

// ─── Board dimensions ────────────────────────────────────────────────────────
const int WIDTH  = 30;
const int HEIGHT = 20;

// ─── Structs ──────────────────────────────────────────────────────────────────
struct Point { int x, y; };

enum Direction { UP, DOWN, LEFT, RIGHT };

// ─── Platform helpers ─────────────────────────────────────────────────────────
#ifdef _WIN32

void clearScreen() {
    COORD coord = {0, 0};
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h, &csbi);
    DWORD size = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD written;
    FillConsoleOutputCharacter(h, ' ', size, coord, &written);
    FillConsoleOutputAttribute(h, csbi.wAttributes, size, coord, &written);
    SetConsoleCursorPosition(h, coord);
}

void hideCursor() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci; ci.dwSize = 1; ci.bVisible = FALSE;
    SetConsoleCursorInfo(h, &ci);
}

void moveCursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

bool kbhitAvailable() { return _kbhit() != 0; }

char getKey() { return _getch(); }

void setColor(int c) { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), c); }
// Colors: 2=green, 4=red, 14=yellow, 11=cyan, 7=white, 15=bright white, 8=gray

#else

// Linux / macOS terminal helpers
struct termios orig_termios;

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disableRawMode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

void clearScreen() { cout << "\033[2J\033[H"; cout.flush(); }

void hideCursor()  { cout << "\033[?25l"; cout.flush(); }
void showCursor()  { cout << "\033[?25h"; cout.flush(); }

void moveCursor(int x, int y) {
    cout << "\033[" << (y+1) << ";" << (x+1) << "H";
}

bool kbhitAvailable() {
    fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    timeval tv = {0, 0};
    return select(STDIN_FILENO+1, &fds, nullptr, nullptr, &tv) > 0;
}

char getKey() {
    char c = 0; read(STDIN_FILENO, &c, 1); return c;
}

void setColor(int c) {
    switch(c) {
        case 2:  cout << "\033[32m";   break; // green
        case 10: cout << "\033[92m";   break; // bright green
        case 4:  cout << "\033[31m";   break; // red
        case 12: cout << "\033[91m";   break; // bright red
        case 14: cout << "\033[93m";   break; // yellow
        case 11: cout << "\033[96m";   break; // cyan
        case 7:  cout << "\033[37m";   break; // white
        case 15: cout << "\033[97m";   break; // bright white
        case 8:  cout << "\033[90m";   break; // gray
        case 6:  cout << "\033[33m";   break; // dark yellow
        default: cout << "\033[0m";    break; // reset
    }
}

#endif

void resetColor() { setColor(0); }

// ─── Game class ───────────────────────────────────────────────────────────────
class SnakeGame {
private:
    deque<Point>  snake;
    Point         food;
    Direction     dir;
    Direction     pendingDir;
    bool          gameOver;
    bool          paused;
    int           score;
    int           hiScore;
    int           level;
    int           speedMs;   // ms per game tick
    string        board[HEIGHT];

    void spawnFood() {
        while (true) {
            int fx = rand() % WIDTH;
            int fy = rand() % HEIGHT;
            bool onSnake = false;
            for (auto& s : snake)
                if (s.x == fx && s.y == fy) { onSnake = true; break; }
            if (!onSnake) { food = {fx, fy}; return; }
        }
    }

    bool collision(Point p) {
        if (p.x < 0 || p.x >= WIDTH || p.y < 0 || p.y >= HEIGHT) return true;
        // skip head (index 0) — we just moved there
        for (int i = 1; i < (int)snake.size(); i++)
            if (snake[i].x == p.x && snake[i].y == p.y) return true;
        return false;
    }

    void drawBorder() {
        // Top border
        setColor(8);
        moveCursor(0, 0);
        cout << "+" << string(WIDTH * 2, '-') << "+";
        // Bottom border
        moveCursor(0, HEIGHT + 1);
        cout << "+" << string(WIDTH * 2, '-') << "+";
        // Side borders
        for (int y = 1; y <= HEIGHT; y++) {
            moveCursor(0, y);         cout << "|";
            moveCursor(WIDTH*2+1, y); cout << "|";
        }
        resetColor();
    }

    void drawCell(int x, int y) {
        // Each cell is 2 chars wide for a nicer square ratio
        moveCursor(1 + x*2, 1 + y);
        Point p = {x, y};

        if (snake[0].x == x && snake[0].y == y) {
            setColor(10); cout << "@@";
        } else if (food.x == x && food.y == y) {
            setColor(12); cout << "()";
        } else {
            bool isSnake = false;
            for (int i = 1; i < (int)snake.size(); i++) {
                if (snake[i].x == x && snake[i].y == y) {
                    // gradient: head=bright, tail=dim
                    int shade = (i < (int)snake.size()/3) ? 10 : (i < (int)snake.size()*2/3 ? 2 : 8);
                    setColor(shade); cout << "##"; isSnake = true; break;
                }
            }
            if (!isSnake) { setColor(0); cout << "  "; }
        }
        resetColor();
    }

    void drawHUD() {
        int hx = WIDTH*2 + 4;

        setColor(10);
        moveCursor(hx, 1);  cout << "  SNAKE  ";
        setColor(8);
        moveCursor(hx, 2);  cout << "---------";

        setColor(14);
        moveCursor(hx, 4);  cout << "SCORE";
        setColor(15);
        moveCursor(hx, 5);  cout << score;
        cout << "      "; // clear old digits

        setColor(14);
        moveCursor(hx, 7);  cout << "BEST";
        setColor(15);
        moveCursor(hx, 8);  cout << hiScore;
        cout << "      ";

        setColor(14);
        moveCursor(hx, 10); cout << "LEVEL";
        setColor(15);
        moveCursor(hx, 11); cout << level;
        cout << "    ";

        setColor(14);
        moveCursor(hx, 13); cout << "LENGTH";
        setColor(15);
        moveCursor(hx, 14); cout << snake.size();
        cout << "    ";

        setColor(8);
        moveCursor(hx, 16); cout << "---------";
        setColor(7);
        moveCursor(hx, 17); cout << "W/Up   ^";
        moveCursor(hx, 18); cout << "S/Dn   v";
        moveCursor(hx, 19); cout << "A/Lt   <";
        moveCursor(hx, 20); cout << "D/Rt   >";
        moveCursor(hx, 21); cout << "P   pause";
        moveCursor(hx, 22); cout << "Q    quit";
        resetColor();
    }

    void handleInput() {
        while (kbhitAvailable()) {
            char c = getKey();
#ifndef _WIN32
            // On Linux/mac arrow keys come as ESC [ A/B/C/D
            if (c == '\033') {
                char seq[2];
                read(STDIN_FILENO, &seq[0], 1);
                read(STDIN_FILENO, &seq[1], 1);
                if (seq[0] == '[') {
                    if (seq[1] == 'A') c = 'w';
                    if (seq[1] == 'B') c = 's';
                    if (seq[1] == 'C') c = 'd';
                    if (seq[1] == 'D') c = 'a';
                }
            }
#else
            // On Windows arrow keys are 0 or 224 followed by a code
            if (c == 0 || c == (char)224) {
                char c2 = _getch();
                if (c2 == 72) c = 'w'; // Up
                if (c2 == 80) c = 's'; // Down
                if (c2 == 75) c = 'a'; // Left
                if (c2 == 77) c = 'd'; // Right
            }
#endif
            switch (c) {
                case 'w': case 'W': if (dir != DOWN)  pendingDir = UP;    break;
                case 's': case 'S': if (dir != UP)    pendingDir = DOWN;  break;
                case 'a': case 'A': if (dir != RIGHT) pendingDir = LEFT;  break;
                case 'd': case 'D': if (dir != LEFT)  pendingDir = RIGHT; break;
                case 'p': case 'P': paused = !paused;                     break;
                case 'q': case 'Q': gameOver = true;                       return;
            }
        }
    }

    void update() {
        dir = pendingDir;
        Point head = snake.front();
        if (dir == UP)    head.y--;
        if (dir == DOWN)  head.y++;
        if (dir == LEFT)  head.x--;
        if (dir == RIGHT) head.x++;

        if (collision(head)) { gameOver = true; return; }

        snake.push_front(head);

        if (head.x == food.x && head.y == food.y) {
            score++;
            if (score > hiScore) hiScore = score;
            level = score / 5 + 1;
            speedMs = max(60, 200 - (level - 1) * 15);
            spawnFood();
            // redraw food cell + old tail position (tail stays)
            drawCell(food.x, food.y);
        } else {
            Point tail = snake.back();
            snake.pop_back();
            drawCell(tail.x, tail.y); // erase tail
        }

        // draw new head and second segment
        drawCell(snake[0].x, snake[0].y);
        if (snake.size() > 1) drawCell(snake[1].x, snake[1].y);

        drawHUD();
    }

public:
    SnakeGame() : hiScore(0) {}

    void init() {
        srand((unsigned)time(nullptr));
        snake.clear();
        snake.push_front({WIDTH/2,     HEIGHT/2});
        snake.push_front({WIDTH/2 + 1, HEIGHT/2});
        snake.push_front({WIDTH/2 + 2, HEIGHT/2});
        dir = RIGHT; pendingDir = RIGHT;
        score = 0; level = 1; speedMs = 200;
        gameOver = false; paused = false;
        spawnFood();
    }

    void fullRedraw() {
        clearScreen();
        drawBorder();
        for (int y = 0; y < HEIGHT; y++)
            for (int x = 0; x < WIDTH; x++)
                drawCell(x, y);
        drawHUD();
    }

    void showScreen(const string& title, const string& sub, const string& prompt) {
        clearScreen();
        int cx = WIDTH/2;   // board center x in cells
        int cy = HEIGHT/2;

        // Draw a centered box
        int bx = cx*2 - 10;
        int by = cy - 4;

        setColor(10);
        moveCursor(bx, by);     cout << "+--------------------+";
        moveCursor(bx, by+1);   cout << "|                    |";
        moveCursor(bx, by+2);   cout << "|                    |";
        moveCursor(bx, by+3);   cout << "|                    |";
        moveCursor(bx, by+4);   cout << "|                    |";
        moveCursor(bx, by+5);   cout << "|                    |";
        moveCursor(bx, by+6);   cout << "+--------------------+";

        setColor(14);
        moveCursor(bx + (22 - (int)title.size())/2, by+2);
        cout << title;

        setColor(7);
        moveCursor(bx + (22 - (int)sub.size())/2, by+4);
        cout << sub;

        setColor(11);
        moveCursor(bx + (22 - (int)prompt.size())/2, by+6);
        cout << prompt;

        resetColor();
        cout.flush();
    }

    void run() {
#ifndef _WIN32
        enableRawMode();
#endif
        hideCursor();

        while (true) {
            // ── Start screen ──────────────────────────────────────────────
            showScreen(" SNAKE ", "Arrow/WASD to move", "Press SPACE to start");
            while (true) {
                if (kbhitAvailable()) {
                    char c = getKey();
                    if (c == ' ' || c == '\r' || c == '\n') break;
                    if (c == 'q' || c == 'Q') goto quit;
                }
                this_thread::sleep_for(milliseconds(50));
            }

            // ── Game loop ─────────────────────────────────────────────────
            init();
            fullRedraw();

            auto lastTick = steady_clock::now();

            while (!gameOver) {
                handleInput();

                if (!paused) {
                    auto now = steady_clock::now();
                    auto elapsed = duration_cast<milliseconds>(now - lastTick).count();
                    if (elapsed >= speedMs) {
                        lastTick = now;
                        update();
                        if (!gameOver) cout.flush();
                    }
                } else {
                    // Draw PAUSE indicator
                    moveCursor(WIDTH - 4, 0);
                    setColor(14); cout << "PAUSED"; resetColor();
                    cout.flush();
                }

                this_thread::sleep_for(milliseconds(10));
            }

            // ── Game over screen ──────────────────────────────────────────
            string scoreLine = "Score: " + to_string(score) + "  Best: " + to_string(hiScore);
            showScreen("GAME OVER", scoreLine, "R=Retry  Q=Quit");

            while (true) {
                if (kbhitAvailable()) {
                    char c = getKey();
                    if (c == 'r' || c == 'R') break;
                    if (c == 'q' || c == 'Q') goto quit;
                }
                this_thread::sleep_for(milliseconds(50));
            }
        }

        quit:
#ifndef _WIN32
        showCursor();
        disableRawMode();
#endif
        clearScreen();
        setColor(10);
        cout << "\n  Thanks for playing Snake!\n\n";
        setColor(14);
        cout << "  Final score : " << score << "\n";
        cout << "  Best score  : " << hiScore << "\n\n";
        resetColor();
    }
};

// ─── Entry point ─────────────────────────────────────────────────────────────
int main() {
    SnakeGame game;
    game.run();
    return 0;
}
