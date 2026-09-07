#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <random>
#include <emscripten/emscripten.h>

enum Color { WHITE = 0, BLACK = 1, BOTH = 2 };
enum Piece {
    EMPTY = 0,
    W_PAWN = 1, W_KNIGHT = 2, W_BISHOP = 3, W_ROOK = 4, W_QUEEN = 5, W_KING = 6,
    B_PAWN = 7, B_KNIGHT = 8, B_BISHOP = 9, B_ROOK = 10, B_QUEEN = 11, B_KING = 12
};

const int PIECE_VALUES[13] = {
    0, 100, 320, 330, 500, 900, 20000,
       100, 320, 330, 500, 900, 20000
};

const int INFINITY_SCORE = 1000000;
const int MATE_SCORE     = 900000;

const int PAWN_PST[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};

const int KNIGHT_PST[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

const int KING_PST[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20
};

struct Move {
    int from = 0;
    int to = 0;
    int promo = EMPTY;
    int score = 0;

    bool operator==(const Move& other) const {
        return from == other.from && to == other.to && promo == other.promo;
    }
};

inline int get_piece_color(int piece) {
    if (piece == EMPTY) return BOTH;
    return (piece <= W_KING) ? WHITE : BLACK;
}

inline std::string square_to_str(int sq) {
    char file = 'a' + (sq % 8);
    char rank = '1' + (sq / 8);
    return {file, rank};
}

inline int str_to_square(const std::string& s) {
    return (s[0] - 'a') + (s[1] - '1') * 8;
}

uint64_t zobrist_pieces[13][64];
uint64_t zobrist_side;

void init_zobrist() {
    static bool initialized = false;
    if (initialized) return;
    std::mt19937_64 rng(1234567ULL);
    for (int p = 0; p < 13; ++p) {
        for (int sq = 0; sq < 64; ++sq) {
            zobrist_pieces[p][sq] = rng();
        }
    }
    zobrist_side = rng();
    initialized = true;
}

enum TTFlag { TT_EXACT, TT_ALPHA, TT_BETA };

struct TTEntry {
    uint64_t key = 0;
    int score = 0;
    int depth = -1;
    TTFlag flag = TT_EXACT;
    Move best_move;
};

const int TT_SIZE = 1 << 18;
TTEntry transposition_table[TT_SIZE];

void tt_store(uint64_t key, int depth, int score, TTFlag flag, Move best_move) {
    int idx = key & (TT_SIZE - 1);
    transposition_table[idx] = {key, score, depth, flag, best_move};
}

bool tt_probe(uint64_t key, int depth, int alpha, int beta, int& score, Move& best_move) {
    int idx = key & (TT_SIZE - 1);
    const TTEntry& e = transposition_table[idx];
    if (e.key == key) {
        best_move = e.best_move;
        if (e.depth >= depth) {
            if (e.flag == TT_EXACT) { score = e.score; return true; }
            if (e.flag == TT_ALPHA && e.score <= alpha) { score = alpha; return true; }
            if (e.flag == TT_BETA && e.score >= beta) { score = beta; return true; }
        }
    }
    return false;
}

struct Board {
    int squares[64];
    Color side_to_move = WHITE;
    int fullmove_number = 1;
    uint64_t zobrist_hash = 0;

    void recompute_hash() {
        zobrist_hash = 0;
        for (int sq = 0; sq < 64; ++sq) {
            if (squares[sq] != EMPTY) {
                zobrist_hash ^= zobrist_pieces[squares[sq]][sq];
            }
        }
        if (side_to_move == BLACK) zobrist_hash ^= zobrist_side;
    }

    void parse_fen(const std::string& fen) {
        for (int i = 0; i < 64; ++i) squares[i] = EMPTY;
        int r = 7, f = 0;
        size_t i = 0;
        for (; i < fen.size() && fen[i] != ' '; ++i) {
            char c = fen[i];
            if (c == '/') { r--; f = 0; }
            else if (c >= '1' && c <= '8') { f += (c - '0'); }
            else {
                int sq = r * 8 + f;
                switch (c) {
                    case 'P': squares[sq] = W_PAWN; break;
                    case 'N': squares[sq] = W_KNIGHT; break;
                    case 'B': squares[sq] = W_BISHOP; break;
                    case 'R': squares[sq] = W_ROOK; break;
                    case 'Q': squares[sq] = W_QUEEN; break;
                    case 'K': squares[sq] = W_KING; break;
                    case 'p': squares[sq] = B_PAWN; break;
                    case 'n': squares[sq] = B_KNIGHT; break;
                    case 'b': squares[sq] = B_BISHOP; break;
                    case 'r': squares[sq] = B_ROOK; break;
                    case 'q': squares[sq] = B_QUEEN; break;
                    case 'k': squares[sq] = B_KING; break;
                }
                f++;
            }
        }
        if (i < fen.size() && fen[i] == ' ') {
            i++;
            if (i < fen.size()) {
                side_to_move = (fen[i] == 'w') ? WHITE : BLACK;
            }
        }
        std::stringstream ss(fen.substr(i));
        std::string token;
        int counter = 0;
        while (ss >> token) {
            counter++;
            if (counter == 5) {
                try { fullmove_number = std::stoi(token); } catch (...) { fullmove_number = 5; }
            }
        }
        recompute_hash();
    }

    int get_king_square(Color c) const {
        int target = (c == WHITE) ? W_KING : B_KING;
        for (int i = 0; i < 64; ++i) {
            if (squares[i] == target) return i;
        }
        return -1;
    }
};

bool is_square_attacked(const Board& b, int sq, Color attacker_color) {
    int r = sq / 8, f = sq % 8;
    if (attacker_color == WHITE) {
        if (r > 0 && f > 0 && b.squares[sq - 9] == W_PAWN) return true;
        if (r > 0 && f < 7 && b.squares[sq - 7] == W_PAWN) return true;
    } else {
        if (r < 7 && f > 0 && b.squares[sq + 7] == B_PAWN) return true;
        if (r < 7 && f < 7 && b.squares[sq + 9] == B_PAWN) return true;
    }

    const int knight_offsets[8][2] = {
        {2, 1}, {2, -1}, {-2, 1}, {-2, -1},
        {1, 2}, {1, -2}, {-1, 2}, {-1, -2}
    };
    int enemy_knight = (attacker_color == WHITE) ? W_KNIGHT : B_KNIGHT;
    for (auto& o : knight_offsets) {
        int nr = r + o[0], nf = f + o[1];
        if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
            if (b.squares[nr * 8 + nf] == enemy_knight) return true;
        }
    }

    const int ortho_dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int enemy_rook  = (attacker_color == WHITE) ? W_ROOK : B_ROOK;
    int enemy_queen = (attacker_color == WHITE) ? W_QUEEN : B_QUEEN;
    for (auto& d : ortho_dirs) {
        int nr = r + d[0], nf = f + d[1];
        while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
            int p = b.squares[nr * 8 + nf];
            if (p != EMPTY) {
                if (p == enemy_rook || p == enemy_queen) return true;
                break;
            }
            nr += d[0]; nf += d[1];
        }
    }

    const int diag_dirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int enemy_bishop = (attacker_color == WHITE) ? W_BISHOP : B_BISHOP;
    for (auto& d : diag_dirs) {
        int nr = r + d[0], nf = f + d[1];
        while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
            int p = b.squares[nr * 8 + nf];
            if (p != EMPTY) {
                if (p == enemy_bishop || p == enemy_queen) return true;
                break;
            }
            nr += d[0]; nf += d[1];
        }
    }

    int enemy_king = (attacker_color == WHITE) ? W_KING : B_KING;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int df = -1; df <= 1; ++df) {
            if (dr == 0 && df == 0) continue;
            int nr = r + dr, nf = f + df;
            if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                if (b.squares[nr * 8 + nf] == enemy_king) return true;
            }
        }
    }
    return false;
}

inline bool is_in_check(const Board& b, Color side) {
    int king_sq = b.get_king_square(side);
    if (king_sq == -1) return false;
    return is_square_attacked(b, king_sq, (side == WHITE) ? BLACK : WHITE);
}

void generate_moves(const Board& b, std::vector<Move>& move_list, bool captures_only = false) {
    Color us = b.side_to_move;
    Color them = (us == WHITE) ? BLACK : WHITE;

    for (int sq = 0; sq < 64; ++sq) {
        int p = b.squares[sq];
        if (p == EMPTY || get_piece_color(p) != us) continue;

        int r = sq / 8, f = sq % 8;

        if (p == W_PAWN || p == B_PAWN) {
            int forward = (us == WHITE) ? 8 : -8;
            int start_rank = (us == WHITE) ? 1 : 6;
            int promo_rank = (us == WHITE) ? 7 : 0;

            if (!captures_only) {
                int to_sq = sq + forward;
                if (to_sq >= 0 && to_sq < 64 && b.squares[to_sq] == EMPTY) {
                    if (to_sq / 8 == promo_rank) {
                        move_list.push_back({sq, to_sq, (us == WHITE) ? W_QUEEN : B_QUEEN});
                    } else {
                        move_list.push_back({sq, to_sq});
                        if (r == start_rank && b.squares[sq + forward * 2] == EMPTY) {
                            move_list.push_back({sq, sq + forward * 2});
                        }
                    }
                }
            }

            int cap_offsets[2] = {(us == WHITE) ? 7 : -9, (us == WHITE) ? 9 : -7};
            for (int offset : cap_offsets) {
                int to_sq = sq + offset;
                if (to_sq >= 0 && to_sq < 64 && std::abs((to_sq % 8) - f) == 1) {
                    if (b.squares[to_sq] != EMPTY && get_piece_color(b.squares[to_sq]) == them) {
                        if (to_sq / 8 == promo_rank) {
                            move_list.push_back({sq, to_sq, (us == WHITE) ? W_QUEEN : B_QUEEN});
                        } else {
                            move_list.push_back({sq, to_sq});
                        }
                    }
                }
            }
        } else if (p == W_KNIGHT || p == B_KNIGHT) {
            const int offsets[8][2] = {
                {2, 1}, {2, -1}, {-2, 1}, {-2, -1},
                {1, 2}, {1, -2}, {-1, 2}, {-1, -2}
            };
            for (auto& o : offsets) {
                int nr = r + o[0], nf = f + o[1];
                if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                    int to_sq = nr * 8 + nf;
                    int dest_p = b.squares[to_sq];
                    if (dest_p == EMPTY) {
                        if (!captures_only) move_list.push_back({sq, to_sq});
                    } else if (get_piece_color(dest_p) == them) {
                        move_list.push_back({sq, to_sq});
                    }
                }
            }
        } else if (p == W_KING || p == B_KING) {
            for (int dr = -1; dr <= 1; ++dr) {
                for (int df = -1; df <= 1; ++df) {
                    if (dr == 0 && df == 0) continue;
                    int nr = r + dr, nf = f + df;
                    if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                        int to_sq = nr * 8 + nf;
                        int dest_p = b.squares[to_sq];
                        if (dest_p == EMPTY) {
                            if (!captures_only) move_list.push_back({sq, to_sq});
                        } else if (get_piece_color(dest_p) == them) {
                            move_list.push_back({sq, to_sq});
                        }
                    }
                }
            }
        } else {
            bool is_diag = (p == W_BISHOP || p == B_BISHOP || p == W_QUEEN || p == B_QUEEN);
            bool is_ortho = (p == W_ROOK || p == B_ROOK || p == W_QUEEN || p == B_QUEEN);

            const int dirs[8][2] = {
                {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
                {1, 0}, {-1, 0}, {0, 1}, {0, -1}
            };

            for (int d = 0; d < 8; ++d) {
                if (d < 4 && !is_diag) continue;
                if (d >= 4 && !is_ortho) continue;

                int nr = r + dirs[d][0], nf = f + dirs[d][1];
                while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                    int to_sq = nr * 8 + nf;
                    int dest_p = b.squares[to_sq];
                    if (dest_p == EMPTY) {
                        if (!captures_only) move_list.push_back({sq, to_sq});
                    } else {
                        if (get_piece_color(dest_p) == them) {
                            move_list.push_back({sq, to_sq});
                        }
                        break;
                    }
                    nr += dirs[d][0]; nf += dirs[d][1];
                }
            }
        }
    }
}

bool make_move(Board& b, const Move& m, int& captured_piece) {
    captured_piece = b.squares[m.to];
    int moving_piece = b.squares[m.from];

    if (moving_piece == EMPTY || get_piece_color(moving_piece) != b.side_to_move) {
        return false;
    }

    if (captured_piece != EMPTY && get_piece_color(captured_piece) == b.side_to_move) {
        return false;
    }

    b.squares[m.from] = EMPTY;
    b.squares[m.to] = (m.promo != EMPTY) ? m.promo : moving_piece;

    if (is_in_check(b, b.side_to_move)) {
        b.squares[m.from] = moving_piece;
        b.squares[m.to] = captured_piece;
        return false;
    }

    b.side_to_move = (b.side_to_move == WHITE) ? BLACK : WHITE;
    if (b.side_to_move == WHITE) b.fullmove_number++;
    b.recompute_hash();
    return true;
}

void undo_move(Board& b, const Move& m, int captured_piece) {
    b.side_to_move = (b.side_to_move == WHITE) ? BLACK : WHITE;
    if (b.side_to_move == BLACK) b.fullmove_number--;

    int placed_piece = b.squares[m.to];
    int original_piece = (m.promo != EMPTY) ? 
        ((b.side_to_move == WHITE) ? W_PAWN : B_PAWN) : placed_piece;

    b.squares[m.from] = original_piece;
    b.squares[m.to] = captured_piece;
    b.recompute_hash();
}

int evaluate_board(const Board& b) {
    int score = 0;
    for (int sq = 0; sq < 64; ++sq) {
        int p = b.squares[sq];
        if (p == EMPTY) continue;

        int piece_val = PIECE_VALUES[p];
        int pst_val = 0;
        int flipped_sq = (p >= B_PAWN) ? (sq ^ 56) : sq;

        if (p == W_PAWN || p == B_PAWN) pst_val = PAWN_PST[flipped_sq];
        else if (p == W_KNIGHT || p == B_KNIGHT) pst_val = KNIGHT_PST[flipped_sq];
        else if (p == W_KING || p == B_KING) pst_val = KING_PST[flipped_sq];

        if (p <= W_KING) score += piece_val + pst_val;
        else score -= (piece_val + pst_val);
    }
    return (b.side_to_move == WHITE) ? score : -score;
}

void score_moves(const Board& b, std::vector<Move>& moves, const Move& tt_best) {
    for (auto& m : moves) {
        if (m == tt_best) {
            m.score = 200000;
            continue;
        }
        int captured = b.squares[m.to];
        if (captured != EMPTY) {
            int attacker = b.squares[m.from];
            m.score = 10000 + PIECE_VALUES[captured] - (PIECE_VALUES[attacker] / 10);
        } else {
            m.score = 0;
        }
    }
    std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
        return a.score > b.score;
    });
}

int quiescence(Board& b, int alpha, int beta) {
    int stand_pat = evaluate_board(b);
    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;

    std::vector<Move> capture_moves;
    generate_moves(b, capture_moves, true);
    score_moves(b, capture_moves, Move());

    for (const auto& m : capture_moves) {
        int captured = EMPTY;
        if (!make_move(b, m, captured)) continue;

        int score = -quiescence(b, -beta, -alpha);
        undo_move(b, m, captured);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

int negamax(Board& b, int depth, int alpha, int beta, int ply, Move& best_move_out) {
    bool in_check = is_in_check(b, b.side_to_move);
    if (in_check) depth++;

    if (depth <= 0) return quiescence(b, alpha, beta);

    Move tt_best;
    int tt_score = 0;
    if (tt_probe(b.zobrist_hash, depth, alpha, beta, tt_score, tt_best)) {
        best_move_out = tt_best;
        return tt_score;
    }

    std::vector<Move> pseudo_moves;
    generate_moves(b, pseudo_moves);
    score_moves(b, pseudo_moves, tt_best);

    int legal_moves_count = 0;
    int max_score = -INFINITY_SCORE;
    Move local_best;
    TTFlag flag = TT_ALPHA;

    for (const auto& m : pseudo_moves) {
        int captured = EMPTY;
        if (!make_move(b, m, captured)) continue;

        legal_moves_count++;
        Move dummy;
        int score = -negamax(b, depth - 1, -beta, -alpha, ply + 1, dummy);
        undo_move(b, m, captured);

        if (score > max_score) {
            max_score = score;
            local_best = m;
        }
        if (score > alpha) {
            alpha = score;
            flag = TT_EXACT;
            if (ply == 0) best_move_out = m;
        }
        if (alpha >= beta) {
            flag = TT_BETA;
            break;
        }
    }

    if (legal_moves_count == 0) {
        if (in_check) return -MATE_SCORE + ply;
        return 0;
    }

    tt_store(b.zobrist_hash, depth, max_score, flag, local_best);
    return max_score;
}

const std::string OPENING_WHITE[4] = {"e2e4", "g1f3", "f1c4", "d2d3"};
const std::string OPENING_BLACK[4] = {"e7e5", "b8c6", "g8f6", "f8c5"};

Move parse_uci_move(const std::string& uci) {
    Move m;
    m.from = str_to_square(uci.substr(0, 2));
    m.to = str_to_square(uci.substr(2, 2));
    return m;
}

Move get_engine_move(Board& b, int search_depth = 4) {
    // CRITICAL FIX: Prevent out-of-bounds array crash by ensuring fullmove_number is valid (>= 1)
    if (b.fullmove_number >= 1 && b.fullmove_number <= 4) {
        std::string book_str = (b.side_to_move == WHITE) ? 
            OPENING_WHITE[b.fullmove_number - 1] : 
            OPENING_BLACK[b.fullmove_number - 1];

        Move book_move = parse_uci_move(book_str);
        int captured = EMPTY;
        if (make_move(b, book_move, captured)) {
            undo_move(b, book_move, captured);
            return book_move;
        }
    }

    Move best_move;
    for (int d = 1; d <= search_depth; ++d) {
        negamax(b, d, -INFINITY_SCORE, INFINITY_SCORE, 0, best_move);
    }
    return best_move;
}
// ============================================================================
// WEBASSEMBLY EXPORT BRIDGE

static std::string final_wasm_move;

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    const char* get_best_move_wasm(const char* fen_string) {
        init_zobrist();
        Board b;
        b.parse_fen(fen_string);
        
        Move best = get_engine_move(b, 4);
        
        // FIX: Construct the UCI string using your custom square_to_str function!
        final_wasm_move = square_to_str(best.from) + square_to_str(best.to);
        
        // Auto-append 'q' if your engine decides to promote a pawn
        if (best.promo != EMPTY) {
            final_wasm_move += "q";
        }
        
        return final_wasm_move.c_str();
    }
}
