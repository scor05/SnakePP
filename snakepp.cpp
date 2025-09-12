/*
    Juego de Snake realizado en ASCII
    Autores: Santiago Cordero, Juan Salguero, Diego Gudiel
*/

#include <ncurses.h>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <sstream>
#include <cstdlib>

using namespace std;


static const char SNAKE_HEAD_CH = 'O';
static const char SNAKE_BODY_CH = 'o';
static const char FOOD_CH = '*';
static const int HUD_HEIGHT = 4;


//Estados
enum class GameState { MENU, INSTRUCTIONS, HIGHSCORES, PLAYER_CREATE, PLAYER_SELECT, 
                        RUNNING, PAUSED, GAMEOVER, EXIT };

struct ScoreEntry { string name; int score; };
struct Point { int row, column; };

//Estructuras de juego
static WINDOW* win_board = nullptr; 
static WINDOW* win_hud = nullptr;
static int maxY = 0, maxX = 0;
static GameState state = GameState::MENU;
static bool running = true;
static pthread_t input_thread;
static pthread_mutex_t ui_mtx = PTHREAD_MUTEX_INITIALIZER;


// Snake en sí
static int dir = KEY_RIGHT;
static vector<Point> snake;
static Point food;
static int speed_us = 100000; // aprox 10 fps

static vector<ScoreEntry> highscores;
static int bestScore = 0;
static bool app_running = true;
static GameState last_drawn = GameState::EXIT;

// Para guardar puntajes destacados
static int score = 0;
static int best_score;
static const char* HIGHSCORES_FILE = "highscores_snake.txt";
static const char* PLAYERS_FILE    = "players_snake.txt";
static string currentPlayer = "Player";
static vector<string> players;
static string playerCreateBuf;
static int playerSelectIdx = 0;


// Funciones de manejo de Jugadores y Puntajes
static void loadPlayers() {
    players.clear();
    ifstream f(PLAYERS_FILE);
    string name;
    while (getline(f, name)) {
        if (!name.empty()) players.push_back(name);
    }
    if (players.empty()) players.push_back("Player");
    if (find(players.begin(), players.end(), currentPlayer) == players.end())
        currentPlayer = players.front();
    playerSelectIdx = 0;
}
static void savePlayers() {
    ofstream f(PLAYERS_FILE, ios::trunc);
    for (auto &p : players) f << p << "\n";
}
static void loadHighscores() {
    highscores.clear();
    ifstream f(HIGHSCORES_FILE);
    if (!f) return;
    string line;
    while (getline(f, line)) {
        if (line.empty()) continue;
        istringstream iss(line);
        string name; int sc;
        if (getline(iss, name, ';') && (iss >> sc)) {
            highscores.push_back({name, sc});
        }
    }
    sort(highscores.begin(), highscores.end(),
         [](const ScoreEntry& a, const ScoreEntry& b){ return a.score > b.score; });
    if (highscores.size() > 20) highscores.resize(20);
}

static void appendHighscore(const string& name, int sc) {
    { ofstream f(HIGHSCORES_FILE, ios::app); if (f) f << name << ';' << sc << "\n"; }
    highscores.push_back({name, sc});
    sort(highscores.begin(), highscores.end(),
         [](const ScoreEntry& a, const ScoreEntry& b){ return a.score > b.score; });
    if (highscores.size() > 20) highscores.resize(20);
}

static int bestScoreFor(const string& name) {
    int best = 0;
    for (auto &e : highscores) if (e.name == name) best = max(best, e.score);
    return best;
}





// Funciones de UI
static void drawBoard() {
    werase(win_board);
    box(win_board, 0, 0);
    mvwaddch(win_board, food.row, food.column, FOOD_CH);
    for (int i = 0; i < snake.size(); i++) {
        char ch = (i == 0 ? SNAKE_HEAD_CH : SNAKE_BODY_CH);
        mvwaddch(win_board, snake[i].row, snake[i].column, ch);
    }
    wrefresh(win_board);
}

static void drawHUD() {
    werase(win_hud);
    box(win_hud, 0, 0);
    mvwprintw(win_hud, 1, 2, "Jugador: %s   Puntaje: %d   Mejor: %d",
              currentPlayer.c_str(), score, bestScore);
    mvwprintw(win_hud, 2, 2, "WASD/Flechas mover | P pausa | Q menu");
    wrefresh(win_hud);
}

static void drawMenu() {
    clear();
    box(stdscr, 0, 0);
    mvprintw(maxY/2 - 4, maxX/2 - 12, "      SNAKE ASCII");
    mvprintw(maxY/2 - 1, maxX/2 - 12, "1) Iniciar partida");
    mvprintw(maxY/2 + 0, maxX/2 - 12, "2) Instrucciones");
    mvprintw(maxY/2 + 1, maxX/2 - 12, "3) Puntajes");
    mvprintw(maxY/2 + 2, maxX/2 - 12, "4) Seleccionar jugador");
    mvprintw(maxY/2 + 3, maxX/2 - 12, "5) Crear jugador");
    mvprintw(maxY/2 + 4, maxX/2 - 12, "6) Salir");
    mvprintw(maxY - 3,2,           "Jugador actual: %s", currentPlayer.c_str());
    refresh();
}

static void drawInstructions() {
    clear();
    box(stdscr, 0,0);
    mvprintw(2,3, "Objetivo: mover la serpiente (cabeza 'O') y comer '*' para sumar puntos.");
    mvprintw(4,3, "Controles: Flechas o WASD para mover. 'Q' para volver al menu.");
    mvprintw(6,3, "Elementos ASCII:");
    mvprintw(6,3, "  O = Cabeza de serpiente");
    mvprintw(7,3, "  o = Cuerpo de serpiente (y cola)");
    mvprintw(8,3, "  * = Comida");
    mvprintw(9,3, "  Paredes delimitadas por líneas");
    mvprintw(maxY-3,3,"Presione Q para volver al menú.");
    refresh();
}

static void drawHighscores() {
    clear();
    box(stdscr, 0, 0);
    mvprintw(2, 3, "PUNTAJES DESTACADOS");
    if (highscores.empty()) {
        mvprintw(4, 5, "Sin registros. Juega y vuelve para ver tus puntajes.");
    } else {
        int row = 4;
        for (int i = 0; i < highscores.size() && row < maxY-3; i++) {
            // Colocar índice, nombre alineados a la izquierda, score a la derecha
            mvprintw(row++, 5, "%2d) %-14s  %5d", i+1,
                     highscores[i].name.c_str(), highscores[i].score);
        }
    }
    mvprintw(maxY-3, 3, "Presiona Q para volver.");
    refresh();
}

static void drawPlayerSelect() {
    pthread_mutex_lock(&ui_mtx);
    clear();
    box(stdscr, 0, 0);
    mvprintw(2, 3, "SELECCIONAR JUGADOR (Flechas Arriba/Abajo, Enter elegir, Q volver)");
    int startRow = 4;
    for (int i = 0; i < players.size() && startRow + i < maxY - 2; i++) {
        if (i == playerSelectIdx) attron(A_REVERSE); // A_REVERSE invierte colores de terminal del texto
        mvprintw(startRow + i, 5, "%s", players[i].c_str());
        if (i == playerSelectIdx) attroff(A_REVERSE);
    }
    refresh();
    pthread_mutex_unlock(&ui_mtx);
}

static void drawPlayerCreate() {
    pthread_mutex_lock(&ui_mtx);
    clear();
    box(stdscr, 0, 0);
    mvprintw(2, 3, "CREAR JUGADOR (Enter guardar, ESC/Q cancelar)");
    mvprintw(4, 3, "Ingrese el nombre del jugador (1 a 16 caracteres): ");
    mvprintw(6, 5, "%s", playerCreateBuf.c_str());
    refresh();
    pthread_mutex_unlock(&ui_mtx);
}

static void drawGameOver() {
    clear();
    box(stdscr, 0, 0);
    mvprintw(maxY/2 - 1, maxX/2 - 6, "GAME OVER");
    mvprintw(maxY/2 + 0, maxX/2 - 14, "Jugador: %s  Puntaje: %d",
             currentPlayer.c_str(), score);
    mvprintw(maxY/2 + 2, maxX/2 - 14, "R: Reiniciar  |  Q: Salir a menú");
    refresh();
}



// ===== Juego =====
static void spawnFood() {
    int h, w; getmaxyx(win_board, h, w);
    food.column = (rand() % (w - 2)) + 1;
    food.row = (rand() % (h - 2)) + 1;
}

static void initGame() {
    snake.clear();
    int h, w; getmaxyx(win_board, h, w);
    snake.push_back({ w/2, h/2 });
    score = 0;
    dir = KEY_RIGHT;
    spawnFood();
    bestScore = bestScoreFor(currentPlayer);
}

static bool advance() {
    int h, w; getmaxyx(win_board, h, w);
    Point head = snake[0];

    if      (dir == KEY_UP)    head.row--;
    else if (dir == KEY_DOWN)  head.row++;
    else if (dir == KEY_LEFT)  head.column--;
    else if (dir == KEY_RIGHT) head.column++;

  
    if (head.column <= 0 || head.column >= w-1 || head.row <= 0 || head.row >= h-1)
        return false;

    bool eating = (head.column == food.column && head.row == food.row);

    
    if (!eating && !snake.empty()) snake.pop_back();

    for (const auto& p : snake) if (p.column == head.column && p.row == head.row) return false;

    snake.insert(snake.begin(), head);

    if (eating) {
        score++;
        if (score > bestScore) bestScore = score;
        spawnFood();
    }
    return true;
}

void* inputThread(void*) {
    while (app_running) {
        int ch = getch();
        if (state == GameState::RUNNING) {
            if (ch == KEY_UP || ch == 'w' || ch == 'W') dir = KEY_UP;
            else if (ch == KEY_DOWN || ch == 's' || ch == 'S') dir = KEY_DOWN;
            else if (ch == KEY_LEFT || ch == 'a' || ch == 'A') dir = KEY_LEFT;
            else if (ch == KEY_RIGHT || ch == 'd' || ch == 'D') dir = KEY_RIGHT;
            else if (ch == 'p' || ch == 'P') state = GameState::PAUSED;
            else if (ch == 'q' || ch == 'Q') state = GameState::MENU;
        } else if (state == GameState::MENU) {
            if (ch == '1') { initGame(); state = GameState::RUNNING; }
            else if (ch == '2') state = GameState::INSTRUCTIONS;
            else if (ch == '3') state = GameState::HIGHSCORES;
            else if (ch == '4') state = GameState::PLAYER_SELECT;
            else if (ch == '5') { playerCreateBuf.clear(); state = GameState::PLAYER_CREATE; }
            else if (ch == '6' || ch == 'q' || ch == 'Q') state = GameState::EXIT;
        } else if (state == GameState::INSTRUCTIONS || state == GameState::HIGHSCORES) {
            if (ch == 'q' || ch == 'Q') state = GameState::MENU;
        } else if (state == GameState::GAMEOVER) {
            if (ch == 'r' || ch == 'R') { initGame(); state = GameState::RUNNING; }
            else if (ch == 'q' || ch == 'Q') state = GameState::MENU;
        }
        usleep(16000);
    }
    return nullptr;
}

int main(){
    setlocale(LC_ALL, "");
    srand(time(NULL));
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    getmaxyx(stdscr, maxY, maxX);

    win_board = newwin(maxY - HUD_HEIGHT, maxX, 0, 0);
    win_hud   = newwin(HUD_HEIGHT, maxX, maxY - HUD_HEIGHT, 0);

    loadPlayers();
    loadHighscores();
    bestScore = bestScoreFor(currentPlayer);

    pthread_create(&input_thread, nullptr, inputThread, nullptr);

    while (app_running) {
        if (state != last_drawn) {
            if      (state == GameState::MENU)          drawMenu();
            else if (state == GameState::INSTRUCTIONS)  drawInstructions();
            else if (state == GameState :: HIGHSCORES)    drawHighscores();
            else if (state == GameState :: PLAYER_SELECT) drawPlayerSelect();
            else if (state == GameState :: PLAYER_CREATE) drawPlayerCreate();
            else if (state == GameState :: GAMEOVER)      drawGameOver();
            last_drawn = state;
        }

        if (state == GameState::RUNNING) {
            if (!advance()) {
                appendHighscore(currentPlayer, score);
                state == GameState::GAMEOVER; last_drawn = GameState::EXIT;
            } else {
                drawBoard();
                drawHUD();
            }
            usleep(speed_us);
        } else if (state == GameState :: PLAYER_SELECT || state == GameState:: PLAYER_CREATE) {
            if (state == GameState :: PLAYER_SELECT)  drawPlayerSelect();
            if (state == GameState ::PLAYER_CREATE)  drawPlayerCreate();
            usleep(16000);
        } else if (state == GameState :: EXIT) {
            app_running = false;
        } else {
            usleep(16000);
        }
    }

    pthread_join(input_thread, nullptr);
    delwin(win_hud);
    delwin(win_board);
    endwin();
    return 0;

}