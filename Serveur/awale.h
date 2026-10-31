#ifndef AWALE_H
#define AWALE_H

#include <stdbool.h>

void awale_afficher_etat_jeu(const int board[12], const int points[2], int current_player);
bool make_move(int board[12], int points[2], int player, int pit);
bool verif_end_game(int board[12], int points[2], int current_player);

#endif /* AWALE_H */
