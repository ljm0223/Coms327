#include <cstdio>
#include <cstdlib>
#include <curses.h>

#include "chessboard.h"
#include "printer.h"
#include "move.h"
#include "io.h"

int main(int argc, char *argv[])
{
    chessboard cb;
    init_terminal();
    mvprintw(0, 0, "Press Enter to Play");
    curs_set(0);
    mvprintw(1, 0, "                        \n\
    __        __   _                            _           \n\
    \ \      / /__| | ___ ___  _ __ ___   ___  | |_ ___     \n\
     \ \ /\ / / _ \ |/ __/ _ \| '_ ` _ \ / _ \ | __/ _ \    \n\
      \ V  V /  __/ | (_| (_) | | | | | |  __/ | || (_) |   \n\
      _\_/\_/ \___|_|\___\___/|_| |_|_|_|\___|  \__\___/    \n\
     / ___| |__   ___  ___ ___   / ___| __ _ _ __ ___   ___ \n\
    | |   | '_ \ / _ \/ __/ __| | |  _ / _` | '_ ` _ \ / _ \\n\
    | |___| | | |  __/\__ \__ \ | |_| | (_| | | | | | |  __/n\
     \____|_| |_|\___||___/___/  \____|\__,_|_| |_| |_|\___|\n\
                                                ");
    while (getch() != 10);
    curs_set(1);
    clear();

    cb_place_pieces(&cb);
    print_board(&cb);

    do {
        move_turn(&cb);
    } while (!cb.end_game_flag);

    clear();
    if (cb.end_game_flag == 1) {
        mvprintw(0, 0, "                                                ");
        mvprintw(0, 0, "Press Esc to Exit");
        mvprintw(1, 0, "                                        \n\
         _    _ _     _ _         _    _ _                      \n\
        | |  | | |   (_) |       | |  | (_)                     \n\
        | |  | | |__  _| |_ ___  | |  | |_ _ __  ___            \n\
        | |/\\| | '_ \\| | __/ _ \\ | |/\\| | | '_ \\/ __|      \n\
        \\  /\\  / | | | | ||  __/ \\  /\\  / | | | \\__ \\     \n\
         \\/  \\/|_| |_|_|\\__\\___|  \\/  \\/|_|_| |_|___/     \n\
                                                                \n\
                                                                    ");
        curs_set(0);                                       
    } else if (cb.end_game_flag == 2) {
        mvprintw(0, 0, "                                                ");
        mvprintw(0, 0, "Press Esc to Exit");
        attron(COLOR_PAIR(1));
        mvprintw(1, 0, "                                        \n\
        ______ _            _      _    _ _                     \n\
        | ___ \\ |          | |    | |  | (_)                   \n\
        | |_/ / | __ _  ___| | __ | |  | |_ _ __  ___           \n\
        | ___ \\ |/ _` |/ __| |/ / | |/\\| | | \'_ \\/ __|      \n\
        | |_/ / | (_| | (__|   <  \\  /\\  / | | | \\__ \\      \n\
        \\____/|_|\\__,_|\\___|_|\\_\\  \\/  \\/|_|_| |_|___/   \n\
                                                                \n\
                                                                    ");
        attroff(COLOR_PAIR(1));
        curs_set(0);
    } else if (cb.end_game_flag == 3 ) {
        mvprintw(0, 0, "                                                ");
        mvprintw(0, 0, "Press Esc to Exit");
        mvprintw(1, 0, "Draw!");
    }

    while (getch() != 27);
    
    end_terminal();
    return 0;
}