#include <stdio.h>

#include "awale.h"

/* Mise en place du plateau et fonctions de base des regles. */
void demarrer_partie(int board[12], int points[2])
{
   for(int i = 0; i < 12; i++)
   {
      board[i] = 4;
   }
   points[0] = 0;
   points[1] = 0;
}

/* Ces tests servent a determiner le camp et les cases a nourrir ou capturer. */
static int is_player_pit(int pit, int player)
{
   return player == 1 ? pit < 6 : pit >= 6;
}

static int opponent_has_seeds(const int board[12], int player)
{
   int first_pit = player == 1 ? 6 : 0;
   int i = 0;

   for(i = first_pit; i < first_pit + 6; i++)
   {
      if(board[i] > 0)
      {
         return 1;
      }
   }

   return 0;
}

static int move_feeds_opponent(const int board[12], int player, int pit)
{
   int board_pit = (player == 1 ? 0 : 6) + pit - 1;
   int distance = player == 1 ? 6 - board_pit : 12 - board_pit;

   return board[board_pit] >= distance;
}

void awale_afficher_etat_jeu(const int board[12], const int points[2], int current_player)
{
   int i = 0;

   printf("Plateau :\n\n");
   for(i = 11; i >= 6; i--)
   {
      printf("%d  | ", board[i]);
   }
   printf("\n");
   for(i = 0; i < 6; i++)
   {
      printf("%d  | ", board[i]);
   }
   printf("\n\nPoints : joueur 1 = %d, joueur 2 = %d\n", points[0], points[1]);
   printf("Joueur courant : %d\n", current_player);
}

/* Joue un coup legal, seme les graines et applique les captures possibles. */
bool make_move(int board[12], int points[2], int player, int pit)
{
   int board_pit = 0;
   int seeds = 0;
   int position = 0;
   int opponent_empty = 0;
   int opponent_seeds = 0;
   int capture_seeds = 0;
   int capture_count = 0;
   int capture_position = 0;

   if(player < 1 || player > 2 || pit < 1 || pit > 6)
   {
      return false;
   }

   board_pit = (player == 1 ? 0 : 6) + pit - 1;
   if(board[board_pit] == 0)
   {
      return false;
   }

   opponent_empty = !opponent_has_seeds(board, player);
   if(opponent_empty && !move_feeds_opponent(board, player, pit))
   {
      return false;
   }

   seeds = board[board_pit];
   position = board_pit;
   board[board_pit] = 0;

   while(seeds > 0)
   {
      position = (position + 1) % 12;
      if(position == board_pit)
      {
         continue;
      }
      board[position]++;
      seeds--;
   }

   for(int i = player == 1 ? 6 : 0; i < (player == 1 ? 12 : 6); i++)
   {
      opponent_seeds += board[i];
   }

   capture_position = position;
   while(!is_player_pit(capture_position, player) &&
         (board[capture_position] == 2 || board[capture_position] == 3))
   {
      capture_seeds += board[capture_position];
      capture_count++;
      capture_position = (capture_position + 11) % 12;
   }

   if(capture_seeds < opponent_seeds)
   {
      capture_position = position;
      for(int i = 0; i < capture_count; i++)
      {
         points[player - 1] += board[capture_position];
         board[capture_position] = 0;
         capture_position = (capture_position + 11) % 12;
      }
   }

   return true;
}

/* Verifie si la partie continue et collecte les graines lorsqu'elle s'arrete. */
bool verif_end_game(int board[12], int points[2], int current_player)
{
   int own_first_pit = 0;
   int opponent_first_pit = 0;
   int own_has_seeds = 0;
   int i = 0;

   if(current_player < 1 || current_player > 2)
   {
      return false;
   }

   own_first_pit = current_player == 1 ? 0 : 6;
   opponent_first_pit = current_player == 1 ? 6 : 0;

   if(points[0] >= 25 || points[1] >= 25)
   {
      return false;
   }

   for(i = 0; i < 6; i++)
   {
      if(board[own_first_pit + i] > 0)
      {
         own_has_seeds = 1;
         break;
      }
   }

   if(!own_has_seeds)
   {
      for(i = 0; i < 6; i++)
      {
         points[2 - current_player] += board[opponent_first_pit + i];
         board[opponent_first_pit + i] = 0;
      }
      return false;
   }

   if(opponent_has_seeds(board, current_player))
   {
      return true;
   }

   for(i = 0; i < 6; i++)
   {
      int pit = own_first_pit + i;
      if(board[pit] > 0 && move_feeds_opponent(board, current_player, i + 1))
      {
         return true;
      }
   }

   for(i = 0; i < 6; i++)
   {
      int pit = own_first_pit + i;
      points[current_player - 1] += board[pit];
      board[pit] = 0;
   }

   return false;
}
