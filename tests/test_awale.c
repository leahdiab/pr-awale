#include <stdio.h>
#include <string.h>

#include "../Serveur/awale.h"

static int tests_run;
static int tests_failed;

static void verifier(bool condition, const char *description) // affiche le resultat d'un test et met a jour les compteurs
{
   tests_run++;
   if(condition)
   {
      printf("OK  - %s\n", description);
   }
   else
   {
      printf("ECHEC - %s\n", description);
      tests_failed++;
   }
}

static int total_graines(const int board[12], const int points[2]) // retourne le nombre total de graines sur le plateau et dans les points
{
   int total = points[0] + points[1];
   for(int i = 0; i < 12; i++)
   {
      total += board[i];
   }
   return total;
}

static void tester_initialisation_et_coups_invalides(void) // teste l'initialisation du plateau et le rejet des coups invalides
{
   int board[12];
   int points[2];
   int expected_board[12];

   for(int i = 0; i < 12; i++)
   {
      board[i] = -1;
   }
   points[0] = -1;
   points[1] = -1;
   demarrer_partie(board, points);

   int initialized = points[0] == 0 && points[1] == 0;
   for(int i = 0; i < 12; i++)
   {
      initialized = initialized && board[i] == 4;
   }
   verifier(initialized, "initialisation a 4 graines par case et 0 point");

   memcpy(expected_board, board, sizeof board);
   verifier(!make_move(board, points, 0, 1) &&
            !make_move(board, points, 1, 0) &&
            !make_move(board, points, 2, 7) &&
            memcmp(board, expected_board, sizeof board) == 0 &&
            points[0] == 0 && points[1] == 0,
            "rejet des joueurs et cases invalides sans modifier la partie");

   memset(board, 0, sizeof board);
   verifier(!make_move(board, points, 1, 1) &&
            points[0] == 0 && points[1] == 0,
            "rejet d'une case vide sans modifier les points");
}

static void tester_coups_et_captures(void) // teste les coups simples, les captures et les interdictions de capture totale
{
   int board[12] = {0};
   int points[2] = {0, 0};

   board[0] = 1;
   board[1] = 1;
   board[6] = 1;
   verifier(make_move(board, points, 1, 1) &&
            board[0] == 0 && board[1] == 2 && board[6] == 1 &&
            points[0] == 0 && points[1] == 0,
            "coup simple sans capture avec camp adverse deja alimente");

   memset(board, 0, sizeof board);
   points[0] = 0;
   points[1] = 0;
   board[5] = 1;
   board[6] = 1;
   board[7] = 1;
   verifier(make_move(board, points, 1, 6) &&
            board[6] == 0 && board[7] == 1 && points[0] == 2,
            "capture autorisee si une graine reste dans le camp adverse");

   memset(board, 0, sizeof board);
   points[0] = 0;
   points[1] = 0;
   board[4] = 3;
   board[6] = 1;
   board[7] = 1;
   board[8] = 1;
   verifier(make_move(board, points, 1, 5) &&
            board[6] == 0 && board[7] == 0 && board[8] == 1 &&
            points[0] == 4,
            "capture en chaine autorisee si une graine adverse reste");

   memset(board, 0, sizeof board);
   points[0] = 0;
   points[1] = 0;
   board[5] = 1;
   board[6] = 1;
   verifier(make_move(board, points, 1, 6) &&
            board[6] == 2 && points[0] == 0 && points[1] == 0,
            "interdiction de capturer toutes les graines adverses");

   memset(board, 0, sizeof board);
   points[0] = 0;
   points[1] = 0;
   board[4] = 3;
   board[6] = 1;
   board[7] = 1;
   verifier(make_move(board, points, 1, 5) &&
            board[6] == 2 && board[7] == 2 &&
            points[0] == 0 && points[1] == 0,
            "interdiction de la capture en chaine qui vide le camp adverse");

   memset(board, 0, sizeof board);
   points[0] = 0;
   points[1] = 0;
   board[11] = 1;
   board[0] = 1;
   verifier(make_move(board, points, 2, 6) &&
            board[0] == 2 && points[0] == 0 && points[1] == 0,
            "meme interdiction de capture totale pour le joueur 2");

   memset(board, 0, sizeof board);
   points[0] = 0;
   points[1] = 0;
   board[0] = 12;
   int before = total_graines(board, points);
   verifier(make_move(board, points, 1, 1) &&
            board[0] == 0 && total_graines(board, points) == before,
            "tour complet du plateau sans semer dans la case de depart");
}

static void tester_alimentation_et_fins_de_partie(void) // teste l'alimentation du camp adverse et les conditions de fin de partie
{
   int board[12] = {0};
   int points[2] = {0, 0};

   board[0] = 1;
   verifier(!make_move(board, points, 1, 1),
            "coup interdit s'il ne nourrit pas un camp adverse vide");
   board[0] = 6;
   verifier(make_move(board, points, 1, 1) && board[6] == 1,
            "coup autorise s'il nourrit un camp adverse vide");

   memset(board, 0, sizeof board);
   points[0] = 25;
   verifier(!verif_end_game(board, points, 1) &&
            points[0] == 25 && points[1] == 0,
            "fin de partie quand un joueur atteint 25 points");

   points[0] = 0;
   points[1] = 25;
   verifier(!verif_end_game(board, points, 2) &&
            points[0] == 0 && points[1] == 25,
            "fin de partie quand le joueur 2 atteint 25 points");

   points[1] = 0;
   verifier(!verif_end_game(board, points, 0) &&
            points[0] == 0 && points[1] == 0,
            "rejet d'un joueur invalide dans la verification de fin");

   memset(board, 0, sizeof board);
   points[0] = 0;
   points[1] = 0;
   board[6] = 3;
   board[11] = 2;
   verifier(!verif_end_game(board, points, 1) &&
            points[1] == 5 && board[6] == 0 && board[11] == 0,
            "fin si le joueur courant n'a plus de graines et transfert des graines restantes au joueur adverse");

   memset(board, 0, sizeof board);
   points[0] = 0;
   points[1] = 0;
   board[0] = 1;
   board[6] = 1;
   verifier(verif_end_game(board, points, 1),
            "partie continue si le camp adverse peut etre nourri");

   memset(board, 0, sizeof board);
   points[0] = 0;
   points[1] = 0;
   board[0] = 1;
   board[1] = 2;
   verifier(!verif_end_game(board, points, 1) &&
            points[0] == 3 && board[0] == 0 && board[1] == 0,
            "fin si aucun coup ne peut nourrir l'adversaire et collecte des graines restantes");

   memset(board, 0, sizeof board);
   points[0] = 0;
   points[1] = 0;
   board[6] = 1;
   board[11] = 1;
   verifier(verif_end_game(board, points, 2),
            "verification symetrique de l'alimentation pour le joueur 2");
}

int main(void)
{
   tester_initialisation_et_coups_invalides();
   tester_coups_et_captures();
   tester_alimentation_et_fins_de_partie();

   printf("\n%d tests, %d echec(s)\n", tests_run, tests_failed);
   return tests_failed == 0 ? 0 : 1;
}
