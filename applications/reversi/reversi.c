/*
 * reversi.c - Classic 8x8 Reversi / Othello vs. Computer AI for SIX
 *
 * Supports interactive full-screen terminal play (arrow keys / hjkl + Space/Enter,
 * or direct algebraic coordinate input like "d3"), undo ('u'), AI hints ('?'),
 * difficulty selection ('1'=Easy, '2'=Medium, '3'=Hard), and AI vs. AI autoplay (-a).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

#define EMPTY 0
#define BLACK 1   /* Human (@) by default */
#define WHITE 2   /* Computer (O) by default */

#define OPPONENT(p) ((p) == BLACK ? WHITE : BLACK)

static const int DR[8] = { -1, -1, -1,  0, 0,  1, 1, 1 };
static const int DC[8] = { -1,  0,  1, -1, 1, -1, 0, 1 };

/*
 * Positional evaluation weights for an 8x8 Reversi board:
 * - Corners (120) are un-flippable anchors.
 * - X-squares (-40) and C-squares (-20) adjacent to empty corners are dangerous.
 * - Edges (20, 10) and inner sweet-16 squares (5, 3) provide stability.
 */
static const int POS_WEIGHTS[8][8] = {
    { 120, -20,  20,   5,   5,  20, -20, 120 },
    { -20, -40,  -5,  -5,  -5,  -5, -40, -20 },
    {  20,  -5,  15,   3,   3,  15,  -5,  20 },
    {   5,  -5,   3,   3,   3,   3,  -5,   5 },
    {   5,  -5,   3,   3,   3,   3,  -5,   5 },
    {  20,  -5,  15,   3,   3,  15,  -5,  20 },
    { -20, -40,  -5,  -5,  -5,  -5, -40, -20 },
    { 120, -20,  20,   5,   5,  20, -20, 120 }
};

typedef struct {
    unsigned char cells[8][8];
} Board;

typedef struct {
    Board board;
    int cur_r;
    int cur_c;
    char status_msg[96];
} UndoState;

static Board g_board;
static UndoState g_history[64];
static int g_hist_len = 0;

static int g_cur_r = 2;
static int g_cur_c = 3;
static int g_hint_r = -1;
static int g_hint_c = -1;
static int g_last_ai_r = -1;
static int g_last_ai_c = -1;
static int g_difficulty = 2; /* 1=Easy (1-ply), 2=Medium (3-ply), 3=Hard (5-ply) */
static int g_human_color = BLACK;
static int g_auto_mode = 0;
static int g_raw_tty = 0;
static struct termios g_orig_termios;
static char g_status[96] = "Your turn (@). Use arrows/hjkl + Space, or type e.g. d3.";

static void tty_raw_enable(void)
{
    struct termios raw;
    if (!isatty(0))
        return;
    if (tcgetattr(0, &g_orig_termios) < 0)
        return;
    raw = g_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(ICRNL | IXON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &raw) == 0)
        g_raw_tty = 1;
}

static void tty_raw_disable(void)
{
    if (g_raw_tty) {
        tcsetattr(0, TCSANOW, &g_orig_termios);
        g_raw_tty = 0;
    }
}

static void board_init(Board *b)
{
    memset(b, 0, sizeof(*b));
    b->cells[3][3] = WHITE;
    b->cells[3][4] = BLACK;
    b->cells[4][3] = BLACK;
    b->cells[4][4] = WHITE;
}

static void count_discs(const Board *b, int *black_cnt, int *white_cnt)
{
    int r, c;
    *black_cnt = 0;
    *white_cnt = 0;
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            if (b->cells[r][c] == BLACK)
                (*black_cnt)++;
            else if (b->cells[r][c] == WHITE)
                (*white_cnt)++;
        }
    }
}

/*
 * Returns number of opponent discs that would be flipped if `player`
 * places a disc at (r, c). If `apply` is non-zero, also modifies `b`.
 */
static int move_flips(Board *b, int r, int c, int player, int apply)
{
    int opp = OPPONENT(player);
    int total_flipped = 0;
    int d;

    if (r < 0 || r >= 8 || c < 0 || c >= 8)
        return 0;
    if (b->cells[r][c] != EMPTY)
        return 0;

    for (d = 0; d < 8; d++) {
        int nr = r + DR[d];
        int nc = c + DC[d];
        int cnt = 0;

        while (nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && b->cells[nr][nc] == opp) {
            nr += DR[d];
            nc += DC[d];
            cnt++;
        }
        if (cnt > 0 && nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && b->cells[nr][nc] == player) {
            total_flipped += cnt;
            if (apply) {
                int step;
                nr = r + DR[d];
                nc = c + DC[d];
                for (step = 0; step < cnt; step++) {
                    b->cells[nr][nc] = (unsigned char)player;
                    nr += DR[d];
                    nc += DC[d];
                }
            }
        }
    }

    if (total_flipped > 0 && apply)
        b->cells[r][c] = (unsigned char)player;

    return total_flipped;
}

static int has_any_move(const Board *b, int player)
{
    int r, c;
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            if (move_flips((Board *)b, r, c, player, 0) > 0)
                return 1;
        }
    }
    return 0;
}

static int count_legal_moves(const Board *b, int player)
{
    int r, c, moves = 0;
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            if (move_flips((Board *)b, r, c, player, 0) > 0)
                moves++;
        }
    }
    return moves;
}

/*
 * Static evaluation from `player`'s perspective:
 * Combines positional weights (dynamically adjusting X/C squares when a corner
 * is already occupied), mobility, and endgame exact disc differential.
 */
static int evaluate_board(const Board *b, int player)
{
    int opp = OPPONENT(player);
    int my_discs = 0, opp_discs = 0;
    int pos_score = 0;
    int r, c;

    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            int cell = b->cells[r][c];
            if (cell == EMPTY)
                continue;
            if (cell == player) {
                my_discs++;
                pos_score += POS_WEIGHTS[r][c];
            } else {
                opp_discs++;
                pos_score -= POS_WEIGHTS[r][c];
            }
        }
    }

    /* If a corner is already taken, adjacent squares are no longer dangerous */
    {
        static const int corners[4][2] = { {0,0}, {0,7}, {7,0}, {7,7} };
        int i;
        for (i = 0; i < 4; i++) {
            int cr = corners[i][0], cc = corners[i][1];
            int owner = b->cells[cr][cc];
            if (owner != EMPTY) {
                int dr = (cr == 0) ? 1 : -1;
                int dc = (cc == 0) ? 1 : -1;
                int sign = (owner == player) ? 1 : -1;
                if (b->cells[cr + dr][cc] == owner) pos_score += sign * 35;
                if (b->cells[cr][cc + dc] == owner) pos_score += sign * 35;
                if (b->cells[cr + dr][cc + dc] == owner) pos_score += sign * 50;
            }
        }
    }

    if (my_discs == 0)
        return -50000;
    if (opp_discs == 0)
        return 50000;

    /* In the last 10 plies, disc count becomes increasingly decisive */
    if (my_discs + opp_discs >= 58)
        return (my_discs - opp_discs) * 100 + pos_score;

    {
        int my_mob = count_legal_moves(b, player);
        int opp_mob = count_legal_moves(b, opp);
        return pos_score + (my_mob - opp_mob) * 8 + (my_discs - opp_discs);
    }
}

static int negamax(const Board *b, int depth, int alpha, int beta, int player)
{
    int opp = OPPONENT(player);
    int r, c;
    int best = -100000;
    int found_move = 0;

    if (depth <= 0)
        return evaluate_board(b, player);

    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            Board next = *b;
            if (move_flips(&next, r, c, player, 1) > 0) {
                int val;
                found_move = 1;
                val = -negamax(&next, depth - 1, -beta, -alpha, opp);
                if (val > best)
                    best = val;
                if (best > alpha)
                    alpha = best;
                if (alpha >= beta)
                    return alpha;
            }
        }
    }

    if (!found_move) {
        /* If neither player can move, game is over: score exact disc diff */
        if (!has_any_move(b, opp)) {
            int b_cnt, w_cnt;
            count_discs(b, &b_cnt, &w_cnt);
            if (player == BLACK)
                return (b_cnt - w_cnt) * 1000;
            else
                return (w_cnt - b_cnt) * 1000;
        }
        /* Pass turn to opponent */
        return -negamax(b, depth - 1, -beta, -alpha, opp);
    }

    return best;
}

static int choose_ai_move(const Board *b, int player, int difficulty, int *out_r, int *out_c)
{
    int r, c;
    int best_score = -200000;
    int best_r = -1, best_c = -1;
    int b_cnt, w_cnt, empties;
    int depth;

    count_discs(b, &b_cnt, &w_cnt);
    empties = 64 - (b_cnt + w_cnt);

    if (difficulty <= 1) {
        depth = 1;
    } else if (difficulty == 2) {
        depth = 3;
    } else {
        depth = (empties <= 8) ? empties : 5;
    }

    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            Board next = *b;
            int flips = move_flips(&next, r, c, player, 1);
            if (flips > 0) {
                int score;
                if (difficulty == 1) {
                    /* Easy mode: mostly greedy flips + slight corner awareness */
                    score = flips * 10 + POS_WEIGHTS[r][c] / 4;
                } else {
                    score = -negamax(&next, depth - 1, -100000, 100000, OPPONENT(player));
                }
                if (score > best_score) {
                    best_score = score;
                    best_r = r;
                    best_c = c;
                }
            }
        }
    }

    if (best_r >= 0) {
        *out_r = best_r;
        *out_c = best_c;
        return 1;
    }
    return 0;
}

static void save_undo_state(void)
{
    if (g_hist_len < 64) {
        g_history[g_hist_len].board = g_board;
        g_history[g_hist_len].cur_r = g_cur_r;
        g_history[g_hist_len].cur_c = g_cur_c;
        strncpy(g_history[g_hist_len].status_msg, g_status, sizeof(g_status) - 1);
        g_hist_len++;
    }
}

static void render_board(void)
{
    int r, c;
    int b_cnt, w_cnt;
    int valid_cnt = count_legal_moves(&g_board, g_human_color);
    const char *diff_name = (g_difficulty == 1) ? "Easy (1-ply)" :
                            (g_difficulty == 2) ? "Medium (3-ply)" : "Hard (5-ply)";

    count_discs(&g_board, &b_cnt, &w_cnt);

    if (g_raw_tty)
        printf("\033[2J\033[H");
    else
        printf("\n");

    printf("  ======================================================================\n");
    printf("   SIX REVERSI (OTHELLO)         Black (@): %2d   |   White (O): %2d\n", b_cnt, w_cnt);
    printf("  ======================================================================\n\n");

    printf("        a     b     c     d     e     f     g     h\n");
    printf("     +-----+-----+-----+-----+-----+-----+-----+-----+\n");

    for (r = 0; r < 8; r++) {
        printf("  %d  |", r + 1);
        for (c = 0; c < 8; c++) {
            int cell = g_board.cells[r][c];
            int is_cur = (g_raw_tty && r == g_cur_r && c == g_cur_c);
            int is_hint = (r == g_hint_r && c == g_hint_c);
            int is_last_ai = (r == g_last_ai_r && c == g_last_ai_c);
            int is_legal = (cell == EMPTY && move_flips(&g_board, r, c, g_human_color, 0) > 0);

            char lb = is_cur ? '[' : (is_hint ? '<' : (is_last_ai ? '(' : ' '));
            char rb = is_cur ? ']' : (is_hint ? '>' : (is_last_ai ? ')' : ' '));

            if (cell == BLACK) {
                if (g_raw_tty)
                    printf(" %c\033[1;36m@\033[0m%c |", lb, rb);
                else
                    printf(" %c@%c |", lb, rb);
            } else if (cell == WHITE) {
                if (g_raw_tty)
                    printf(" %c\033[1;33mO\033[0m%c |", lb, rb);
                else
                    printf(" %cO%c |", lb, rb);
            } else if (is_legal) {
                if (g_raw_tty)
                    printf(" %c\033[1;32m.\033[0m%c |", lb, rb);
                else
                    printf(" %c.%c |", lb, rb);
            } else {
                printf(" %c %c |", lb, rb);
            }
        }

        /* Side info panel */
        if (r == 0)
            printf("   AI Level : %s", diff_name);
        else if (r == 1)
            printf("   Your Side: Black (@)  [%d legal move%s]", valid_cnt, valid_cnt == 1 ? "" : "s");
        else if (r == 3)
            printf("   Controls :");
        else if (r == 4)
            printf("     Arrows / hjkl : Move cursor [ ]");
        else if (r == 5)
            printf("     Space / Enter : Place disc at cursor");
        else if (r == 6)
            printf("     a-h + 1-8     : Direct move (e.g. d3)");
        else if (r == 7)
            printf("     ? Hint | u Undo | 1-3 Level | n New | q Quit");

        printf("\n     +-----+-----+-----+-----+-----+-----+-----+-----+\n");
    }

    printf("\n  Status: %s\n", g_status);
}

static void check_and_run_ai_turns(void)
{
    int ai_color = OPPONENT(g_human_color);

    while (1) {
        int human_moves = has_any_move(&g_board, g_human_color);
        int ai_moves = has_any_move(&g_board, ai_color);

        if (!human_moves && !ai_moves) {
            int b_cnt, w_cnt;
            count_discs(&g_board, &b_cnt, &w_cnt);
            if (b_cnt > w_cnt)
                snprintf(g_status, sizeof(g_status),
                         "GAME OVER! You (@) win %d to %d! Press 'n' for a new game or 'q' to quit.",
                         b_cnt, w_cnt);
            else if (w_cnt > b_cnt)
                snprintf(g_status, sizeof(g_status),
                         "GAME OVER! Computer (O) wins %d to %d! Press 'n' for a new game or 'q' to quit.",
                         w_cnt, b_cnt);
            else
                snprintf(g_status, sizeof(g_status),
                         "GAME OVER! Draw (%d - %d)! Press 'n' for a new game or 'q' to quit.",
                         b_cnt, w_cnt);
            return;
        }

        if (!ai_moves) {
            snprintf(g_status, sizeof(g_status),
                     "Computer (O) has no legal moves and passes! Your turn (@).");
            return;
        }

        /* Computer makes its move */
        {
            int ar = -1, ac = -1, flipped;
            choose_ai_move(&g_board, ai_color, g_difficulty, &ar, &ac);
            flipped = move_flips(&g_board, ar, ac, ai_color, 1);
            g_last_ai_r = ar;
            g_last_ai_c = ac;

            if (has_any_move(&g_board, g_human_color)) {
                snprintf(g_status, sizeof(g_status),
                         "Computer (O) played %c%d (flipped %d). Your turn (@).",
                         'a' + ac, ar + 1, flipped);
                return;
            } else if (has_any_move(&g_board, ai_color)) {
                snprintf(g_status, sizeof(g_status),
                         "Computer played %c%d; you have no legal moves, so Computer moves again...",
                         'a' + ac, ar + 1);
                render_board();
                if (g_raw_tty)
                    usleep(500000);
                continue;
            }
        }
    }
}

static int play_human_move(int r, int c)
{
    int flipped;

    if (move_flips(&g_board, r, c, g_human_color, 0) <= 0) {
        snprintf(g_status, sizeof(g_status),
                 "Illegal move %c%d! Choose a square marked with '.' (must flip at least 1 O).",
                 'a' + c, r + 1);
        return 0;
    }

    save_undo_state();
    g_hint_r = -1;
    g_hint_c = -1;
    flipped = move_flips(&g_board, r, c, g_human_color, 1);
    snprintf(g_status, sizeof(g_status),
             "You (@) played %c%d (flipped %d).", 'a' + c, r + 1, flipped);

    check_and_run_ai_turns();
    return 1;
}

static int run_autoplay_demo(int max_turns)
{
    int player = BLACK;
    int turns = 0;
    int b_cnt, w_cnt;

    board_init(&g_board);
    while (turns < max_turns) {
        int r = -1, c = -1;
        if (choose_ai_move(&g_board, player, g_difficulty, &r, &c)) {
            int flipped = move_flips(&g_board, r, c, player, 1);
            g_last_ai_r = r;
            g_last_ai_c = c;
            snprintf(g_status, sizeof(g_status),
                     "Autoplay turn %d: %s played %c%d (flipped %d).",
                     turns + 1, player == BLACK ? "Black (@)" : "White (O)",
                     'a' + c, r + 1, flipped);
            turns++;
            player = OPPONENT(player);
        } else if (has_any_move(&g_board, OPPONENT(player))) {
            player = OPPONENT(player);
        } else {
            break;
        }
    }

    count_discs(&g_board, &b_cnt, &w_cnt);
    snprintf(g_status, sizeof(g_status),
             "Autoplay complete after %d moves -- Final Score: Black (@) %d, White (O) %d.",
             turns, b_cnt, w_cnt);
    render_board();
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    int pending_col = -1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-1") == 0)
            g_difficulty = 1;
        else if (strcmp(argv[i], "-2") == 0)
            g_difficulty = 2;
        else if (strcmp(argv[i], "-3") == 0)
            g_difficulty = 3;
        else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--auto") == 0)
            g_auto_mode = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: reversi [-1|-2|-3] [-a]\n");
            printf("  -1   Easy AI (1-ply)\n");
            printf("  -2   Medium AI (3-ply, default)\n");
            printf("  -3   Hard AI (5-ply alpha-beta + exact endgame)\n");
            printf("  -a   Auto-play a full AI vs. AI game and print result\n");
            return 0;
        }
    }

    if (g_auto_mode)
        return run_autoplay_demo(60);

    board_init(&g_board);
    tty_raw_enable();

    render_board();

    while (1) {
        unsigned char ch;
        if (read(0, &ch, 1) != 1)
            break;

        if (ch == 'q' || ch == 'Q' || ch == 3 /* Ctrl-C */) {
            break;
        } else if (ch == 27) {
            /* Escape sequence for arrow keys: ESC [ A/B/C/D */
            unsigned char seq[2];
            if (read(0, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(0, &seq[1], 1) == 1) {
                    if (seq[1] == 'A' && g_cur_r > 0) g_cur_r--;
                    else if (seq[1] == 'B' && g_cur_r < 7) g_cur_r++;
                    else if (seq[1] == 'C' && g_cur_c < 7) g_cur_c++;
                    else if (seq[1] == 'D' && g_cur_c > 0) g_cur_c--;
                }
            }
            pending_col = -1;
        } else if (ch == 'k' || ch == 'K') {
            if (g_cur_r > 0) g_cur_r--;
            pending_col = -1;
        } else if (ch == 'j' || ch == 'J') {
            if (g_cur_r < 7) g_cur_r++;
            pending_col = -1;
        } else if (ch == 'h' || ch == 'H') {
            if (g_cur_c > 0) g_cur_c--;
            pending_col = -1;
        } else if (ch == 'l' || ch == 'L') {
            if (g_cur_c < 7) g_cur_c++;
            pending_col = -1;
        } else if (ch == ' ' || ch == '\r' || ch == '\n') {
            pending_col = -1;
            play_human_move(g_cur_r, g_cur_c);
        } else if (ch >= 'a' && ch <= 'h') {
            pending_col = ch - 'a';
            g_cur_c = pending_col;
            snprintf(g_status, sizeof(g_status),
                     "Column '%c' selected -- now press row 1-8 (or Space to play at [%c%d]).",
                     ch, 'a' + g_cur_c, g_cur_r + 1);
        } else if (ch >= '1' && ch <= '8') {
            if (pending_col >= 0) {
                g_cur_r = ch - '1';
                g_cur_c = pending_col;
                pending_col = -1;
                play_human_move(g_cur_r, g_cur_c);
            } else if (ch >= '1' && ch <= '3') {
                g_difficulty = ch - '0';
                snprintf(g_status, sizeof(g_status),
                         "Difficulty changed to %d (%s).",
                         g_difficulty,
                         g_difficulty == 1 ? "Easy" : (g_difficulty == 2 ? "Medium" : "Hard"));
            }
        } else if (ch == '?') {
            int hr = -1, hc = -1;
            pending_col = -1;
            if (choose_ai_move(&g_board, g_human_color, 3, &hr, &hc)) {
                g_hint_r = hr;
                g_hint_c = hc;
                g_cur_r = hr;
                g_cur_c = hc;
                snprintf(g_status, sizeof(g_status),
                         "AI Hint: Best move is %c%d (marked with <.> and cursor moved there).",
                         'a' + hc, hr + 1);
            } else {
                snprintf(g_status, sizeof(g_status), "No legal moves available.");
            }
        } else if (ch == 'u' || ch == 'U') {
            pending_col = -1;
            if (g_hist_len > 0) {
                g_hist_len--;
                g_board = g_history[g_hist_len].board;
                g_cur_r = g_history[g_hist_len].cur_r;
                g_cur_c = g_history[g_hist_len].cur_c;
                g_hint_r = -1;
                g_hint_c = -1;
                g_last_ai_r = -1;
                g_last_ai_c = -1;
                snprintf(g_status, sizeof(g_status), "Undid last turn. Your turn (@).");
            } else {
                snprintf(g_status, sizeof(g_status), "Nothing to undo!");
            }
        } else if (ch == 'n' || ch == 'N') {
            pending_col = -1;
            board_init(&g_board);
            g_hist_len = 0;
            g_hint_r = -1;
            g_hint_c = -1;
            g_last_ai_r = -1;
            g_last_ai_c = -1;
            g_cur_r = 2;
            g_cur_c = 3;
            snprintf(g_status, sizeof(g_status), "Started a new game! Your turn (@).");
        } else {
            continue;
        }

        render_board();
    }

    tty_raw_disable();
    printf("\nThanks for playing SIX Reversi!\n");
    return 0;
}
