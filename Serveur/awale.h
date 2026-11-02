#ifndef AWALE_H
#define AWALE_H

#include <stdbool.h>

void awale_afficher_etat_jeu(const int board[12], const int points[2], int current_player);
bool make_move(int board[12], int points[2], int player, int pit);
bool verif_end_game(int board[12], int points[2], int current_player);

#endif /* AWALE_H */


/*
 * awale.h
 * - faire une structure avec les usernames et les numeros de parties
 * - associer à chaque joueur le numero de partie ? 
 * - associer a chaque joueur une variable disponible 
 * - serveur affiche liste des joueurs connectés et disponibles pour jouer
 * - 
 */