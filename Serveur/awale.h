#ifndef AWALE_H
#define AWALE_H

#include <stdbool.h>

/* Fonctions publiques des regles et de l'affichage du plateau. */
void demarrer_partie(int board[12], int points[2]);
void awale_afficher_etat_jeu(const int board[12], const int points[2], int current_player);
bool make_move(int board[12], int points[2], int player, int pit);
bool verif_end_game(int board[12], int points[2], int current_player);

#endif
