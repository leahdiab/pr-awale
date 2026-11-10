#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>

#include "server2.h"
#include "client2.h"
#include "awale.h"

/* Donnees temporaires des relations entre joueurs. */
typedef struct FriendLink
{
   char player1[BUF_SIZE];
   char player2[BUF_SIZE];
   struct FriendLink *next;
} FriendLink;

typedef struct FriendRequest
{
   char sender[BUF_SIZE];
   char recipient[BUF_SIZE];
   struct FriendRequest *next;
} FriendRequest;

static FriendLink *friendships;
static FriendRequest *friend_requests;

static void liberer_partie(Game *game);

/* Initialisation reseau du serveur. */
static void init(void)
{
#ifdef WIN32
   WSADATA wsa;
   int err = WSAStartup(MAKEWORD(2, 2), &wsa);
   if(err < 0)
   {
      puts("WSAStartup failed !");
      exit(EXIT_FAILURE);
   }
#endif
}

static void end(void)
{
#ifdef WIN32
   WSACleanup();
#endif
}

/* Affichage du plateau et recherche des joueurs ou parties actifs. */
bool traiter_coup(int board[12], int points[2], int current_player, int pit)
{
   return make_move(board, points, current_player, pit);
}

void envoyer_etat_jeu(SOCKET sock, const int board[12], const int points[2],
                      const char *username, const char *top_username,
                      const char *bottom_username)
{
   char buffer[BUF_SIZE];
   snprintf(buffer, BUF_SIZE, "Plateau :\n\n");
   for(int i = 11; i >= 6; i--)
   {
      snprintf(buffer + strlen(buffer), BUF_SIZE - strlen(buffer), "%d  | ", board[i]);
   }
   snprintf(buffer + strlen(buffer), BUF_SIZE - strlen(buffer), "  %s\n", top_username);
   for(int i = 0; i < 6; i++)
   {
      snprintf(buffer + strlen(buffer), BUF_SIZE - strlen(buffer), "%d  | ", board[i]);
   }
   snprintf(buffer + strlen(buffer), BUF_SIZE - strlen(buffer), "  %s\n\n", bottom_username);
   snprintf(buffer + strlen(buffer), BUF_SIZE - strlen(buffer), "Points : joueur 1 = %d, joueur 2 = %d\n", points[0], points[1]);
   snprintf(buffer + strlen(buffer), BUF_SIZE - strlen(buffer), "Joueur courant : %s\n", username);

   write_client(sock, buffer);
}

static int trouver_client_par_pseudo(Client *clients, int actual, const char *username)
{
   for(int i = 0; i < actual; i++)
   {
      if(strcmp(clients[i].name, username) == 0)
      {
         return i;
      }
   }

   return -1;
}

static int trouver_partie_par_socket(Game *games, SOCKET sock)
{
   for(int i = 0; i < MAX_CLIENTS / 2; i++)
   {
      if(games[i].active &&
         (games[i].player1.sock == sock || games[i].player2.sock == sock))
      {
         return i;
      }
   }

   return -1;
}

/* Gestion des amities et des demandes en attente. */
static bool sont_amis(const char *player1, const char *player2)
{
   for(FriendLink *link = friendships; link != NULL; link = link->next)
   {
      if((strcmp(link->player1, player1) == 0 &&
          strcmp(link->player2, player2) == 0) ||
         (strcmp(link->player1, player2) == 0 &&
          strcmp(link->player2, player1) == 0))
      {
         return true;
      }
   }
   return false;
}

static bool ajouter_lien_amitie(const char *player1, const char *player2)
{
   FriendLink *link;
   FILE *file;

   if(sont_amis(player1, player2))
   {
      return true;
   }

   link = malloc(sizeof *link);
   if(link == NULL)
   {
      perror("malloc(amitie)");
      return false;
   }
   snprintf(link->player1, sizeof link->player1, "%s", player1);
   snprintf(link->player2, sizeof link->player2, "%s", player2);

   file = fopen("friends.txt", "a");
   if(file == NULL)
   {
      perror("fopen(friends.txt)");
      free(link);
      return false;
   }

   bool saved = fprintf(file, "%s\t%s\n", player1, player2) >= 0;
   if(fclose(file) != 0)
   {
      perror("fclose(friends.txt)");
      saved = false;
   }
   if(!saved)
   {
      fprintf(stderr, "Erreur pendant l'ecriture de friends.txt\n");
      free(link);
      return false;
   }

   link->next = friendships;
   friendships = link;
   return true;
}

static bool supprimer_lien_amitie(const char *player1, const char *player2)
{
   const char *temporary_path = "friends.txt.tmp";
   FILE *source;
   FILE *temporary;
   bool removed = false;

   if(!sont_amis(player1, player2))
   {
      return false;
   }

   source = fopen("friends.txt", "r");
   if(source == NULL)
   {
      perror("fopen(friends.txt)");
      return false;
   }

   temporary = fopen(temporary_path, "w");
   if(temporary == NULL)
   {
      perror("fopen(friends.txt.tmp)");
      fclose(source);
      return false;
   }

   char line[2 * BUF_SIZE + 2];
   bool success = true;
   while(fgets(line, sizeof line, source) != NULL)
   {
      char *separator = strchr(line, '\t');
      if(separator == NULL)
      {
         fprintf(stderr, "Ligne invalide dans friends.txt\n");
         success = false;
         break;
      }

      *separator = '\0';
      char *stored_player2 = separator + 1;
      stored_player2[strcspn(stored_player2, "\r\n")] = '\0';
      bool matching_link =
         (strcmp(line, player1) == 0 && strcmp(stored_player2, player2) == 0) ||
         (strcmp(line, player2) == 0 && strcmp(stored_player2, player1) == 0);

      *separator = '\t';
      if(matching_link)
      {
         removed = true;
      }
      else if(fputs(line, temporary) == EOF)
      {
         perror("fputs(friends.txt.tmp)");
         success = false;
         break;
      }
   }

   if(ferror(source))
   {
      perror("fgets(friends.txt)");
      success = false;
   }
   if(fclose(source) != 0)
   {
      perror("fclose(friends.txt)");
      success = false;
   }
   if(fclose(temporary) != 0)
   {
      perror("fclose(friends.txt.tmp)");
      success = false;
   }

   if(!success || !removed)
   {
      if(!removed && success)
      {
         fprintf(stderr, "Amitie absente de friends.txt\n");
      }
      if(remove(temporary_path) != 0)
      {
         perror("remove(friends.txt.tmp)");
      }
      return false;
   }

   if(rename(temporary_path, "friends.txt") != 0)
   {
      perror("rename(friends.txt.tmp, friends.txt)");
      if(remove(temporary_path) != 0)
      {
         perror("remove(friends.txt.tmp)");
      }
      return false;
   }

   FriendLink **link = &friendships;
   while(*link != NULL)
   {
      if((strcmp((*link)->player1, player1) == 0 &&
          strcmp((*link)->player2, player2) == 0) ||
         (strcmp((*link)->player1, player2) == 0 &&
          strcmp((*link)->player2, player1) == 0))
      {
         FriendLink *removed_link = *link;
         *link = removed_link->next;
         free(removed_link);
         break;
      }
      link = &(*link)->next;
   }

   return true;
}

static bool charger_amitiees(void)
{
   FILE *file = fopen("friends.txt", "r");
   if(file == NULL)
   {
      if(errno == ENOENT)
      {
         return true;
      }
      perror("fopen(friends.txt)");
      return false;
   }

   char line[2 * BUF_SIZE + 2];
   while(fgets(line, sizeof line, file) != NULL)
   {
      char *separator = strchr(line, '\t');
      if(separator == NULL)
      {
         fprintf(stderr, "Ligne invalide dans friends.txt\n");
         fclose(file);
         return false;
      }
      *separator = '\0';
      char *player2 = separator + 1;
      size_t player1_length = strlen(line);
      size_t player2_length = strcspn(player2, "\r\n");
      player2[player2_length] = '\0';
      if(player1_length == 0 || player1_length >= BUF_SIZE ||
         player2_length == 0 || player2_length >= BUF_SIZE)
      {
         fprintf(stderr, "Ligne invalide dans friends.txt\n");
         fclose(file);
         return false;
      }
      if(!sont_amis(line, player2))
      {
         FriendLink *link = malloc(sizeof *link);
         if(link == NULL)
         {
            perror("malloc(amitie)");
            fclose(file);
            return false;
         }
         memcpy(link->player1, line, player1_length + 1);
         memcpy(link->player2, player2, player2_length + 1);
         link->next = friendships;
         friendships = link;
      }
   }

   if(ferror(file))
   {
      perror("fgets(friends.txt)");
      fclose(file);
      return false;
   }
   if(fclose(file) != 0)
   {
      perror("fclose(friends.txt)");
      return false;
   }
   return true;
}

static bool ajouter_demande_amitie(const char *sender, const char *recipient)
{
   for(FriendRequest *request = friend_requests; request != NULL;
       request = request->next)
   {
      if(strcmp(request->sender, sender) == 0 &&
         strcmp(request->recipient, recipient) == 0)
      {
         return false;
      }
   }

   FriendRequest *request = malloc(sizeof *request);
   if(request == NULL)
   {
      perror("malloc(demande d'amitie)");
      return false;
   }
   snprintf(request->sender, sizeof request->sender, "%s", sender);
   snprintf(request->recipient, sizeof request->recipient, "%s", recipient);
   request->next = friend_requests;
   friend_requests = request;
   return true;
}

static bool accepter_demande_amitie(const char *sender, const char *recipient)
{
   FriendRequest **request = &friend_requests;
   while(*request != NULL)
   {
      if(strcmp((*request)->sender, sender) == 0 &&
         strcmp((*request)->recipient, recipient) == 0)
      {
         if(!ajouter_lien_amitie(sender, recipient))
         {
            return false;
         }
         FriendRequest *accepted = *request;
         *request = accepted->next;
         free(accepted);
         return true;
      }
      request = &(*request)->next;
   }
   return false;
}

static bool refuser_demande_amitie(const char *sender, const char *recipient)
{
   FriendRequest **request = &friend_requests;
   while(*request != NULL)
   {
      if(strcmp((*request)->sender, sender) == 0 &&
         strcmp((*request)->recipient, recipient) == 0)
      {
         FriendRequest *refused = *request;
         *request = refused->next;
         free(refused);
         return true;
      }
      request = &(*request)->next;
   }
   return false;
}

static void liberer_amitiees(void)
{
   while(friendships != NULL)
   {
      FriendLink *link = friendships;
      friendships = link->next;
      free(link);
   }
   while(friend_requests != NULL)
   {
      FriendRequest *request = friend_requests;
      friend_requests = request->next;
      free(request);
   }
}

/* Historique et fichiers de sauvegarde ou de reprise des parties. */
static int trouver_sauvegarde_en_attente(Game *games, SOCKET sock)
{
   for(int i = 0; i < MAX_CLIENTS / 2; i++)
   {
      if(games[i].awaiting_save &&
         (games[i].player1.sock == sock || games[i].player2.sock == sock))
      {
         return i;
      }
   }

   return -1;
}

static bool memoriser_etat_partie(Game *game)
{
   if(game->history_count == game->history_capacity)
   {
      size_t new_capacity = game->history_capacity == 0
         ? 16 : game->history_capacity * 2;
      if(new_capacity <= game->history_capacity ||
         new_capacity > (size_t)-1 / sizeof *game->history)
      {
         fprintf(stderr, "Historique de partie trop volumineux\n");
         return false;
      }

      GameState *history = realloc(game->history, new_capacity * sizeof *history);
      if(history == NULL)
      {
         perror("realloc(historique de partie)");
         return false;
      }

      game->history = history;
      game->history_capacity = new_capacity;
   }

   GameState *state = &game->history[game->history_count++];
   memcpy(state->board, game->board, sizeof state->board);
   memcpy(state->points, game->points, sizeof state->points);
   state->current_player = game->current_player;
   return true;
}

static bool enregistrer_partie(const Game *game)
{
   FILE *file = fopen("saved_games.txt", "a");
   if(file == NULL)
   {
      perror("fopen(saved_games.txt)");
      return false;
   }

   bool success = fprintf(file, "GAME %d\nPLAYER1 %s\nPLAYER2 %s\n",
                          game->num_partie, game->player1.name,
                          game->player2.name) >= 0;
   for(size_t i = 0; i < game->history_count && success; i++)
   {
      const GameState *state = &game->history[i];
      success = fprintf(file, "STATE %zu %d %d %d",
                        i + 1, state->current_player,
                        state->points[0], state->points[1]) >= 0;
      for(int pit = 0; pit < 12 && success; pit++)
      {
         success = fprintf(file, " %d", state->board[pit]) >= 0;
      }
      if(success)
      {
         success = fprintf(file, "\n") >= 0;
      }
   }

   if(success)
   {
      success = fprintf(file, "ENDGAME\n") >= 0;
   }
   if(fclose(file) != 0)
   {
      perror("fclose(saved_games.txt)");
      success = false;
   }
   if(!success)
   {
      fprintf(stderr, "Erreur pendant l'ecriture de saved_games.txt\n");
   }

   return success;
}

static bool lire_champ_sauvegarde(const char *line, const char *prefix,
                                  char *value, size_t value_size)
{
   size_t prefix_size = strlen(prefix);
   if(strncmp(line, prefix, prefix_size) != 0)
   {
      return false;
   }

   size_t value_size_read = strcspn(line + prefix_size, "\r\n");
   if(value_size_read == 0 || value_size_read >= value_size)
   {
      return false;
   }

   memcpy(value, line + prefix_size, value_size_read);
   value[value_size_read] = '\0';
   return true;
}

static bool ecrire_etats_parties_en_cours(Game *games)
{
   const char *temporary_path = "ongoing_games.txt.tmp";
   FILE *file = fopen(temporary_path, "w");
   if(file == NULL)
   {
      perror("fopen(ongoing_games.txt.tmp)");
      return false;
   }

   bool success = true;
   for(int i = 0; i < MAX_CLIENTS / 2 && success; i++)
   {
      const Game *game = &games[i];
      if(!game->active && !game->suspended)
      {
         continue;
      }

      success = fprintf(file,
                        "ONGOING_GAME %d\nPLAYER1 %s\nPLAYER2 %s\n"
                        "CURRENT %d\nPOINTS %d %d\nBOARD",
                        game->num_partie, game->player1.name,
                        game->player2.name, game->current_player,
                        game->points[0], game->points[1]) >= 0;
      for(int pit = 0; pit < 12 && success; pit++)
      {
         success = fprintf(file, " %d", game->board[pit]) >= 0;
      }
      if(success)
      {
         success = fprintf(file,
                           "\nPRIVATE %d\nPRIVACY_PENDING %d\n"
                           "HISTORY_FAILED %d\nHISTORY %zu\n",
                           game->private_game, game->privacy_pending,
                           game->history_failed, game->history_count) >= 0;
      }
      for(size_t state_index = 0;
          state_index < game->history_count && success; state_index++)
      {
         const GameState *state = &game->history[state_index];
         success = fprintf(file, "HSTATE %d %d %d",
                           state->current_player, state->points[0],
                           state->points[1]) >= 0;
         for(int pit = 0; pit < 12 && success; pit++)
         {
            success = fprintf(file, " %d", state->board[pit]) >= 0;
         }
         if(success)
         {
            success = fprintf(file, "\n") >= 0;
         }
      }
      if(success)
      {
         success = fprintf(file, "ENDONGOING\n") >= 0;
      }
   }

   if(fclose(file) != 0)
   {
      perror("fclose(ongoing_games.txt.tmp)");
      success = false;
   }
   if(!success)
   {
      fprintf(stderr, "Erreur pendant l'ecriture de ongoing_games.txt\n");
      remove(temporary_path);
      return false;
   }

#ifdef WIN32
   if(remove("ongoing_games.txt") != 0 && errno != ENOENT)
   {
      perror("remove(ongoing_games.txt)");
      remove(temporary_path);
      return false;
   }
#endif
   if(rename(temporary_path, "ongoing_games.txt") != 0)
   {
      perror("rename(ongoing_games.txt)");
      remove(temporary_path);
      return false;
   }
   return true;
}

static bool lire_ligne_partie_en_cours(FILE *file, char *line,
                                       size_t line_size)
{
   if(fgets(line, (int)line_size, file) == NULL)
   {
      return false;
   }
   line[strcspn(line, "\r\n")] = '\0';
   return true;
}

static bool charger_parties_en_cours(Game *games, int *next_game_id)
{
   FILE *file = fopen("ongoing_games.txt", "r");
   char line[BUF_SIZE * 2];
   int loaded = 0;

   if(file == NULL)
   {
      if(errno == ENOENT)
      {
         return true;
      }
      perror("fopen(ongoing_games.txt)");
      return false;
   }

   while(fgets(line, sizeof line, file) != NULL)
   {
      int game_id;
      if(sscanf(line, "ONGOING_GAME %d", &game_id) != 1 ||
         game_id <= 0 || game_id == INT_MAX ||
         loaded >= MAX_CLIENTS / 2)
      {
         fprintf(stderr, "Fichier ongoing_games.txt invalide\n");
         goto failure;
      }

      Game *game = &games[loaded];
      memset(game, 0, sizeof *game);
      game->num_partie = game_id;
      game->active = false;
      game->suspended = true;
      game->player1.sock = INVALID_SOCKET;
      game->player2.sock = INVALID_SOCKET;

      if(!lire_ligne_partie_en_cours(file, line, sizeof line) ||
         !lire_champ_sauvegarde(line, "PLAYER1 ", game->player1.name,
                                sizeof game->player1.name) ||
         !lire_ligne_partie_en_cours(file, line, sizeof line) ||
         !lire_champ_sauvegarde(line, "PLAYER2 ", game->player2.name,
                                sizeof game->player2.name) ||
         !lire_ligne_partie_en_cours(file, line, sizeof line) ||
         sscanf(line, "CURRENT %d", &game->current_player) != 1 ||
         (game->current_player != 1 && game->current_player != 2) ||
         !lire_ligne_partie_en_cours(file, line, sizeof line) ||
         sscanf(line, "POINTS %d %d", &game->points[0],
                &game->points[1]) != 2 ||
         !lire_ligne_partie_en_cours(file, line, sizeof line) ||
         strncmp(line, "BOARD ", 6) != 0)
      {
         fprintf(stderr, "Fichier ongoing_games.txt invalide\n");
         goto failure;
      }
      int board_values[12];
      char *board_value = line + 6;
      for(int pit = 0; pit < 12; pit++)
      {
         char *next = NULL;
         long value = strtol(board_value, &next, 10);
         if(next == board_value || value < 0 || value > 48)
         {
            fprintf(stderr, "Plateau invalide dans ongoing_games.txt\n");
            goto failure;
         }
         board_values[pit] = (int)value;
         board_value = next;
      }
      while(isspace((unsigned char)*board_value))
      {
         board_value++;
      }
      if(*board_value != '\0')
      {
         fprintf(stderr, "Plateau invalide dans ongoing_games.txt\n");
         goto failure;
      }
      memcpy(game->board, board_values, sizeof game->board);

      int private_game;
      int privacy_pending;
      int history_failed;
      size_t history_count;
      if(!lire_ligne_partie_en_cours(file, line, sizeof line) ||
         sscanf(line, "PRIVATE %d", &private_game) != 1 ||
         (private_game != 0 && private_game != 1) ||
         !lire_ligne_partie_en_cours(file, line, sizeof line) ||
         sscanf(line, "PRIVACY_PENDING %d", &privacy_pending) != 1 ||
         (privacy_pending != 0 && privacy_pending != 1) ||
         !lire_ligne_partie_en_cours(file, line, sizeof line) ||
         sscanf(line, "HISTORY_FAILED %d", &history_failed) != 1 ||
         (history_failed != 0 && history_failed != 1) ||
         !lire_ligne_partie_en_cours(file, line, sizeof line) ||
         sscanf(line, "HISTORY %zu", &history_count) != 1 ||
         history_count > (size_t)-1 / sizeof *game->history)
      {
         fprintf(stderr, "Fichier ongoing_games.txt invalide\n");
         goto failure;
      }
      game->private_game = private_game != 0;
      game->privacy_pending = privacy_pending != 0;
      game->history_failed = history_failed != 0;
      if(history_count > 0)
      {
         game->history = malloc(history_count * sizeof *game->history);
         if(game->history == NULL)
         {
            perror("malloc(historique restaure)");
            goto failure;
         }
         game->history_count = history_count;
         game->history_capacity = history_count;
      }
      for(size_t state_index = 0; state_index < history_count; state_index++)
      {
         int values[15];
         if(!lire_ligne_partie_en_cours(file, line, sizeof line) ||
            sscanf(line, "HSTATE %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                   &values[0], &values[1], &values[2], &values[3],
                   &values[4], &values[5], &values[6], &values[7],
                   &values[8], &values[9], &values[10], &values[11],
                   &values[12], &values[13], &values[14]) != 15 ||
            (values[0] != 1 && values[0] != 2))
         {
            fprintf(stderr, "Historique invalide dans ongoing_games.txt\n");
            goto failure;
         }
         GameState *state = &game->history[state_index];
         state->current_player = values[0];
         state->points[0] = values[1];
         state->points[1] = values[2];
         memcpy(state->board, &values[3], sizeof state->board);
      }
      if(!lire_ligne_partie_en_cours(file, line, sizeof line) ||
         strcmp(line, "ENDONGOING") != 0)
      {
         fprintf(stderr, "Fichier ongoing_games.txt invalide\n");
         goto failure;
      }

      if(game_id >= *next_game_id)
      {
         *next_game_id = game_id + 1;
      }
      loaded++;
   }

   if(ferror(file))
   {
      perror("fgets(ongoing_games.txt)");
      goto failure;
   }
   if(fclose(file) != 0)
   {
      perror("fclose(ongoing_games.txt)");
      return false;
   }
   return true;

failure:
   fclose(file);
   for(int i = 0; i <= loaded && i < MAX_CLIENTS / 2; i++)
   {
      liberer_partie(&games[i]);
   }
   return false;
}

static void envoyer_mes_parties_en_cours(const Game *games,
                                         const char *username, SOCKET sock)
{
   char message[BUF_SIZE] = "Vos parties recuperables :";
   size_t count = 0;
   for(int i = 0; i < MAX_CLIENTS / 2; i++)
   {
      const Game *game = &games[i];
      const char *opponent = NULL;
      if(!game->suspended)
      {
         continue;
      }
      if(strcmp(game->player1.name, username) == 0)
      {
         opponent = game->player2.name;
      }
      else if(strcmp(game->player2.name, username) == 0)
      {
         opponent = game->player1.name;
      }
      if(opponent == NULL)
      {
         continue;
      }

      size_t used = strlen(message);
      int written = snprintf(message + used, sizeof message - used,
                             "\nPartie %d contre %s", game->num_partie,
                             opponent);
      if(written < 0 || (size_t)written >= sizeof message - used)
      {
         strncat(message, "\n(liste tronquee)",
                 sizeof message - strlen(message) - 1);
         break;
      }
      count++;
   }

   if(count == 0)
   {
      strncat(message, "\nAucune partie recuperable.",
              sizeof message - strlen(message) - 1);
   }
   else
   {
      strncat(message, "\nUtilisez RESUME numero_partie pour demander la reprise.",
              sizeof message - strlen(message) - 1);
   }
   write_client(sock, message);
}

static bool envoyer_liste_sauvegardes(SOCKET sock, size_t *game_count)
{
   FILE *file = fopen("saved_games.txt", "r");
   char line[BUF_SIZE * 2];
   char player1[BUF_SIZE];
   char player2[BUF_SIZE];
   char message[BUF_SIZE] = "Parties sauvegardees :";
   size_t count = 0;

   *game_count = 0;
   if(file == NULL)
   {
      if(errno == ENOENT)
      {
         write_client(sock, "Aucune partie sauvegardee.");
         return true;
      }

      perror("fopen(saved_games.txt)");
      write_client(sock, "Impossible de lire les parties sauvegardees.");
      return false;
   }

   while(fgets(line, sizeof line, file) != NULL)
   {
      if(strncmp(line, "GAME ", 5) != 0)
      {
         continue;
      }

      if(fgets(line, sizeof line, file) == NULL ||
         !lire_champ_sauvegarde(line, "PLAYER1 ", player1, sizeof player1) ||
         fgets(line, sizeof line, file) == NULL ||
         !lire_champ_sauvegarde(line, "PLAYER2 ", player2, sizeof player2))
      {
         fclose(file);
         write_client(sock, "Le fichier des parties sauvegardees est invalide.");
         return false;
      }

      size_t used = strlen(message);
      int written = snprintf(message + used, sizeof message - used,
                             "\n%zu. %s vs %s", count + 1, player1, player2);
      if(written < 0 || (size_t)written >= sizeof message - used)
      {
         strncat(message, "\n(liste tronquee)",
                 sizeof message - strlen(message) - 1);
         break;
      }
      count++;
   }

   if(ferror(file))
   {
      perror("fgets(saved_games.txt)");
      fclose(file);
      write_client(sock, "Erreur pendant la lecture des parties sauvegardees.");
      return false;
   }
   if(fclose(file) != 0)
   {
      perror("fclose(saved_games.txt)");
      write_client(sock, "Erreur pendant la lecture des parties sauvegardees.");
      return false;
   }

   if(count == 0)
   {
      write_client(sock, "Aucune partie sauvegardee.");
   }
   else
   {
      *game_count = count;
      strncat(message, "\nEntrez le numero de la partie a revoir.",
              sizeof message - strlen(message) - 1);
      write_client(sock, message);
   }

   return true;
}

static int lire_etat_sauvegarde(size_t game_index, size_t state_index,
                                GameState *state, char *player1,
                                char *player2, size_t name_size,
                                size_t *state_count)
{
   FILE *file = fopen("saved_games.txt", "r");
   char line[BUF_SIZE * 2];
   size_t current_game = 0;

   if(file == NULL)
   {
      perror("fopen(saved_games.txt)");
      return -1;
   }

   while(fgets(line, sizeof line, file) != NULL)
   {
      if(strncmp(line, "GAME ", 5) != 0)
      {
         continue;
      }

      current_game++;
      if(current_game != game_index)
      {
         continue;
      }

      if(fgets(line, sizeof line, file) == NULL ||
         !lire_champ_sauvegarde(line, "PLAYER1 ", player1, name_size) ||
         fgets(line, sizeof line, file) == NULL ||
         !lire_champ_sauvegarde(line, "PLAYER2 ", player2, name_size))
      {
         fclose(file);
         return -1;
      }

      *state_count = 0;
      bool requested_state_found = false;
      while(fgets(line, sizeof line, file) != NULL)
      {
         if(strncmp(line, "ENDGAME", 7) == 0)
         {
            if(state_index <= *state_count && !requested_state_found)
            {
               fclose(file);
               return -1;
            }
            fclose(file);
            return 1;
         }
         if(strncmp(line, "STATE ", 6) == 0)
         {
            size_t saved_state_index;
            GameState saved_state;
            int converted = sscanf(
               line, "STATE %zu %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
               &saved_state_index, &saved_state.current_player,
               &saved_state.points[0], &saved_state.points[1],
               &saved_state.board[0], &saved_state.board[1],
               &saved_state.board[2], &saved_state.board[3],
               &saved_state.board[4], &saved_state.board[5],
               &saved_state.board[6], &saved_state.board[7],
               &saved_state.board[8], &saved_state.board[9],
               &saved_state.board[10], &saved_state.board[11]);
            if(converted != 16)
            {
               fclose(file);
               return -1;
            }

            (*state_count)++;
            if(saved_state_index == state_index)
            {
               *state = saved_state;
               requested_state_found = true;
            }
         }
      }

      fclose(file);
      return -1;
   }

   if(ferror(file))
   {
      perror("fgets(saved_games.txt)");
      fclose(file);
      return -1;
   }
   fclose(file);
   return 0;
}

static bool lire_numero_commande(const char *text, size_t *number)
{
   char *end = NULL;
   long value;

   errno = 0;
   value = strtol(text, &end, 10);
   while(isspace((unsigned char)*end))
   {
      end++;
   }
   if(end == text || *end != '\0' || errno == ERANGE || value <= 0)
   {
      return false;
   }

   *number = (size_t)value;
   return true;
}

static void envoyer_etat_relecture(SOCKET sock, const GameState *state,
                                   const char *player1, const char *player2,
                                   size_t state_index, size_t state_count)
{
   const char *current_player = state->current_player == 1 ? player1 : player2;
   char message[BUF_SIZE];

   snprintf(message, sizeof message, "Coup %zu/%zu. Ecrivez NEXT pour voir le prochain coup.\n",
            state_index, state_count);
   write_client(sock, message);
   envoyer_etat_jeu(sock, state->board, state->points, current_player,
                    player2, player1);
}

static void afficher_coup_sauvegarde(Client *client, size_t game_index,
                                      size_t state_index)
{
   GameState state;
   char player1[BUF_SIZE];
   char player2[BUF_SIZE];
   size_t state_count = 0;
   int result = lire_etat_sauvegarde(game_index, state_index, &state,
                                     player1, player2, sizeof player1,
                                     &state_count);

   if(result < 0)
   {
      write_client(client->sock, "Impossible de lire la partie sauvegardee.");
   }
   else if(result == 0)
   {
      write_client(client->sock, "Cette partie sauvegardee est introuvable.");
   }
   else if(state_count == 0)
   {
      client->reviewing = false;
      write_client(client->sock, "Cette partie ne contient aucun coup a revoir.");
   }
   else if(state_index > state_count)
   {
      client->reviewing = false;
      write_client(client->sock, "Fin de la partie. Ecrivez REVIEW pour en revoir une autre.");
   }
   else
   {
      client->reviewing = true;
      client->review_game_index = game_index;
      client->review_state_index = state_index;
      envoyer_etat_relecture(client->sock, &state, player1, player2,
                             state_index, state_count);
   }
}

/* Gestion des parties en memoire et des spectateurs. */
static void liberer_partie(Game *game)
{
   free(game->history);
   memset(game, 0, sizeof *game);
}

static int trouver_partie_par_numero(Game *games, int numero_partie)
{
   for(int i = 0; i < MAX_CLIENTS / 2; i++)
   {
      if(games[i].active && games[i].num_partie == numero_partie)
      {
         return i;
      }
   }

   return -1;
}

static int trouver_partie_suspendue_par_numero(Game *games, int numero_partie)
{
   for(int i = 0; i < MAX_CLIENTS / 2; i++)
   {
      if(games[i].suspended && games[i].num_partie == numero_partie)
      {
         return i;
      }
   }
   return -1;
}

static bool ajouter_spectateur(Game *game, SOCKET sock)
{
   for(int i = 0; i < game->nombre_spectateurs; i++)
   {
      if(game->spectateurs[i] == sock)
      {
         return true;
      }
   }

   if(game->nombre_spectateurs >= MAX_CLIENTS)
   {
      return false;
   }

   game->spectateurs[game->nombre_spectateurs++] = sock;
   return true;
}

static void retirer_spectateur(Game *game, SOCKET sock)
{
   for(int i = 0; i < game->nombre_spectateurs; i++)
   {
      if(game->spectateurs[i] == sock)
      {
         game->spectateurs[i] = game->spectateurs[--game->nombre_spectateurs];
         return;
      }
   }
}

static bool peut_regarder_partie_privee(const Client *client, const Game *game)
{
   return sont_amis(client->name, game->player1.name) ||
          sont_amis(client->name, game->player2.name);
}

/* Construction des listes envoyees en reponse aux commandes du joueur. */
static void envoyer_liste_amis(const Client *client)
{
   char message[BUF_SIZE] = "Liste d'amis :";
   int count = 0;

   for(FriendLink *link = friendships; link != NULL; link = link->next)
   {
      const char *friend_name = NULL;
      if(strcmp(link->player1, client->name) == 0)
      {
         friend_name = link->player2;
      }
      else if(strcmp(link->player2, client->name) == 0)
      {
         friend_name = link->player1;
      }

      if(friend_name != NULL)
      {
         size_t used = strlen(message);
         int written = snprintf(message + used, sizeof message - used,
                                "\n%s", friend_name);
         if(written < 0 || (size_t)written >= sizeof message - used)
         {
            break;
         }
         count++;
      }
   }

   for(FriendRequest *request = friend_requests; request != NULL;
       request = request->next)
   {
      if(strcmp(request->recipient, client->name) == 0)
      {
         size_t used = strlen(message);
         int written = snprintf(message + used, sizeof message - used,
                                "\nDemande de %s (repondre : ACCEPT FRIEND ou REFUSE FRIEND %s)",
                                request->sender, request->sender);
         if(written < 0 || (size_t)written >= sizeof message - used)
         {
            break;
         }
         count++;
      }
   }

   if(count == 0)
   {
      strncat(message, "\nAucun ami ou demande en attente.",
              sizeof message - strlen(message) - 1);
   }
   write_client(client->sock, message);
}

static bool ajouter_ligne_liste(char *message, size_t message_size,
                                const char *line)
{
   size_t used = strlen(message);
   int written = snprintf(message + used, message_size - used, "\n%s", line);
   return written >= 0 && (size_t)written < message_size - used;
}

static void envoyer_liste_joueurs(Client *clients, int actual, Game *games,
                                  SOCKET requester)
{
   char message[BUF_SIZE] = "Joueurs disponibles :";
   int available_count = 0;
   int playing_count = 0;
   bool truncated = false;

   for(int i = 0; i < actual; i++)
   {
      if(clients[i].available && clients[i].sock != requester)
      {
         if(!ajouter_ligne_liste(message, sizeof message, clients[i].name))
         {
            truncated = true;
            break;
         }
         available_count++;
      }
   }

   if(available_count == 0 &&
      !ajouter_ligne_liste(message, sizeof message, "Aucun joueur disponible."))
   {
      truncated = true;
   }

   if(!ajouter_ligne_liste(message, sizeof message, "Joueurs en partie :"))
   {
      truncated = true;
   }

   for(int g = 0; g < MAX_CLIENTS / 2 && !truncated; g++)
   {
      if(!games[g].active)
      {
         continue;
      }

      Client *players[] = {&games[g].player1, &games[g].player2};
      for(size_t p = 0; p < sizeof players / sizeof players[0]; p++)
      {
         if(players[p]->sock != requester)
         {
            if(!ajouter_ligne_liste(message, sizeof message, players[p]->name))
            {
               truncated = true;
               break;
            }
            playing_count++;
         }
      }
   }

   if(playing_count == 0 && !truncated &&
      !ajouter_ligne_liste(message, sizeof message, "Aucun joueur en partie."))
   {
      truncated = true;
   }

   if(truncated)
   {
      size_t used = strlen(message);
      if(used + sizeof "\n(liste tronquee)" <= sizeof message)
      {
         strncat(message, "\n(liste tronquee)", sizeof message - used - 1);
      }
   }
   write_client(requester, message);
}

static void envoyer_liste_parties(Game *games, SOCKET requester)
{
   char message[BUF_SIZE] = "Parties en cours :";
   int count = 0;

   for(int i = 0; i < MAX_CLIENTS / 2; i++)
   {
      if(games[i].active)
      {
         const char *current_player = games[i].current_player == 1
            ? games[i].player1.name : games[i].player2.name;
         size_t used = strlen(message);
         count++;
         int written = snprintf(message + used, sizeof message - used,
                                "\nPartie %d : %s contre %s (tour de %s)",
                                games[i].num_partie, games[i].player1.name,
                                games[i].player2.name, current_player);
         if(written < 0 || (size_t)written >= sizeof message - used)
         {
            break;
         }
      }
   }

   if(count == 0)
   {
      strncat(message, "\nAucune partie en cours.",
              sizeof message - strlen(message) - 1);
   }

   write_client(requester, message);
}

static void envoyer_etat_spectateur(const Game *game, SOCKET sock)
{
   const char *username = game->current_player == 1
      ? game->player1.name : game->player2.name;

   envoyer_etat_jeu(sock, game->board, game->points, username,
                    game->player2.name, game->player1.name);
}

static void envoyer_etat_partie(const Game *game)
{
   const char *username = game->current_player == 1
      ? game->player1.name : game->player2.name;

   envoyer_etat_jeu(game->player1.sock, game->board, game->points, username,
                    game->player2.name, game->player1.name);
   envoyer_etat_jeu(game->player2.sock, game->board, game->points, username,
                    game->player2.name, game->player1.name);

   for(int i = 0; i < game->nombre_spectateurs; i++)
   {
      envoyer_etat_spectateur(game, game->spectateurs[i]);
   }
}

static void envoyer_fin_partie(const Game *game)
{
   char message[BUF_SIZE];
   int winner = game->points[0] == game->points[1]
      ? 0 : (game->points[0] > game->points[1] ? 1 : 2);

   snprintf(message, sizeof message, "====== FIN DE PARTIE======\nPlateau :\n\n");
   for(int i = 11; i >= 6; i--)
   {
      snprintf(message + strlen(message), sizeof message - strlen(message),
               "%d  | ", game->board[i]);
   }
   snprintf(message + strlen(message), sizeof message - strlen(message),
            "  %.80s\n", game->player2.name);
   for(int i = 0; i < 6; i++)
   {
      snprintf(message + strlen(message), sizeof message - strlen(message),
               "%d  | ", game->board[i]);
   }
   snprintf(message + strlen(message), sizeof message - strlen(message),
            "  %.80s\n\n", game->player1.name);
   snprintf(message + strlen(message), sizeof message - strlen(message),
            "Points : %.80s = %d, %.80s = %d\n",
            game->player1.name, game->points[0],
            game->player2.name, game->points[1]);
   if(winner == 0)
   {
      snprintf(message + strlen(message), sizeof message - strlen(message),
               "Resultat : egalite.\n");
   }
   else
   {
      const char *winner_name = winner == 1
         ? game->player1.name : game->player2.name;
      snprintf(message + strlen(message), sizeof message - strlen(message),
               "Gagnant : %.800s\n", winner_name);
   }

   write_client(game->player1.sock, message);
   write_client(game->player2.sock, message);
   for(int i = 0; i < game->nombre_spectateurs; i++)
   {
      write_client(game->spectateurs[i], message);
   }
}

static int trouver_partie_libre(Game *games)
{
   for(int i = 0; i < MAX_CLIENTS / 2; i++)
   {
      if(!games[i].active && !games[i].suspended &&
         !games[i].awaiting_save)
      {
         return i;
      }
   }

   return -1;
}

static void demander_sauvegarde(Game *games, Game *game,
                                Client *clients, int actual)
{
   const char *spectator_message = "La partie est terminee.";

   for(int i = 0; i < actual; i++)
   {
      if(clients[i].sock == game->player1.sock ||
         clients[i].sock == game->player2.sock)
      {
         clients[i].available = false;
         clients[i].invited_by[0] = '\0';
         write_client(clients[i].sock,
                      "Partie terminee. Voulez-vous sauvegarder le jeu ? Repondez OUI ou NON.");
      }
   }

   for(int i = 0; i < game->nombre_spectateurs; i++)
   {
      write_client(game->spectateurs[i], spectator_message);
   }
   game->nombre_spectateurs = 0;
   game->active = false;
   game->suspended = false;
   game->awaiting_save = true;
   if(!ecrire_etats_parties_en_cours(games))
   {
      fprintf(stderr, "La partie terminee peut encore figurer dans ongoing_games.txt\n");
      write_client(game->player1.sock,
                   "Erreur : la partie terminee n'a pas pu etre retiree du fichier des reprises.");
      write_client(game->player2.sock,
                   "Erreur : la partie terminee n'a pas pu etre retiree du fichier des reprises.");
   }
}

/* Visibilite des parties, aide et informations de profil. */
static void partie_privee(SOCKET sock, Game *game)
{
   game->privacy_pending = true;
   write_client(sock,
                "Choisissez la visibilite de la partie : PRIVATE ou PUBLIC.");
}

static void afficher_commandes(SOCKET sock){
   char buffer[BUF_SIZE * 2] =
      "\n========== Commandes disponibles ==========\n"
      "Aide\n"
      "HELP                         - Affiche cette aide\n\n"
      "Defis et amis\n"
      "LIST PLAYERS                 - Affiche les joueurs connectes\n"
      "CHALLENGE pseudo             - Invite un joueur a jouer\n"
      "LIST FRIENDS                 - Affiche tes amis et demandes\n"
      "ADD FRIEND pseudo            - Envoie une demande d'ami\n"
      "REMOVE FRIEND pseudo         - Supprime un ami\n\n"
      "Parties\n"
      "MOVE numero                  - Joue un coup (1 a 6)\n"
      "END                          - Termine la partie\n"
      "LIST GAMES                   - Affiche les parties en cours\n"
      "WATCH numero                 - Regarde une partie\n"
      "STOP WATCH                   - Arrete de regarder une partie\n"
      "LIST MY GAMES                - Affiche tes parties recuperables\n\n"
      "Communication et profil\n"
      "SEND PUBLIC message          - Envoie un message public\n"
      "SEND PRIVATE pseudo message  - Envoie un message prive\n"
      "WRITE BIO texte              - Ecrit ta biographie\n"
      "READ BIO pseudo              - Lit la biographie d'un joueur\n\n"
      "Relecture\n"
      "REVIEW                       - Revoit une partie sauvegardee\n"
      "===========================================\n";

   write_client(sock, buffer);
}

static void ecrire_biographie( SOCKET sock, Client *clients, int actual, const char *bio){
   char message[BUF_SIZE];
   snprintf(message, sizeof message, "Biographie mise à jour : %s", bio);
   write_client(sock, message);

}

static void lire_biographie( SOCKET sock, Client *clients, int actual, const char *username){
   int target_index = trouver_client_par_pseudo(clients, actual, username);
   if(target_index < 0)
   {
      write_client(sock, "Pseudo introuvable.");
   }
   else
   {
      char message[BUF_SIZE];
      snprintf(message, sizeof message, "%s : %s", clients[target_index].name, clients[target_index].bio);
      write_client(sock, message);
   }

}

/* Fonctions reseau appelees par la boucle principale. */
static void clear_clients(Client *clients, int actual)
{
   int i = 0;
   for(i = 0; i < actual; i++)
   {
      closesocket(clients[i].sock);
   }
}

static void remove_client(Client *clients, int to_remove, int *actual)
{

   memmove(clients + to_remove, clients + to_remove + 1, (*actual - to_remove - 1) * sizeof(Client));

   (*actual)--;
}

static void send_message_to_all_clients(Client *clients, Client sender, int actual, const char *buffer, char from_server)
{
   int i = 0;
   char message[BUF_SIZE];
   message[0] = 0;
   for(i = 0; i < actual; i++)
   {

      if(sender.sock != clients[i].sock)
      {
         if(from_server == 0)
         {
            strncpy(message, sender.name, BUF_SIZE - 1);
            strncat(message, " (public) : ", sizeof message - strlen(message) - 1);
         }
         strncat(message, buffer, sizeof message - strlen(message) - 1);
         write_client(clients[i].sock, message);
      }
   }
}

static int init_connection(void)
{
   SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
   SOCKADDR_IN sin = { 0 };
#ifndef WIN32
   int reuse_address = 1;
#endif

   if(sock == INVALID_SOCKET)
   {
      perror("socket()");
      exit(errno);
   }

#ifndef WIN32
   if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                 sizeof reuse_address) == SOCKET_ERROR)
   {
      perror("setsockopt(SO_REUSEADDR)");
      closesocket(sock);
      exit(errno);
   }
#endif

   sin.sin_addr.s_addr = htonl(INADDR_ANY);
   sin.sin_port = htons(PORT);
   sin.sin_family = AF_INET;

   if(bind(sock,(SOCKADDR *) &sin, sizeof sin) == SOCKET_ERROR)
   {
      perror("bind()");
      exit(errno);
   }

   if(listen(sock, MAX_CLIENTS) == SOCKET_ERROR)
   {
      perror("listen()");
      exit(errno);
   }

   return sock;
}

static void end_connection(int sock)
{
   closesocket(sock);
}

static int read_client(SOCKET sock, char *buffer)
{
   int n = 0;

   if((n = recv(sock, buffer, BUF_SIZE - 1, 0)) < 0)
   {
      perror("recv()");

      n = 0;
   }

   buffer[n] = 0;

   return n;
}

static void write_client(SOCKET sock, const char *buffer)
{
   if(send(sock, buffer, strlen(buffer), 0) < 0)
   {
      perror("send()");
      exit(errno);
   }
}

/* Boucle principale : connexions, deconnexions et commandes des joueurs. */
static void app(void)
{
   SOCKET sock = init_connection();
   char buffer[BUF_SIZE];

   int actual = 0;
   int max = sock;

   Client clients[MAX_CLIENTS] = {0};
   Game games[MAX_CLIENTS / 2] = {0};
   int next_game_id = 1;

   if(!charger_amitiees())
   {
      end_connection(sock);
      exit(EXIT_FAILURE);
   }
   if(!charger_parties_en_cours(games, &next_game_id))
   {
      end_connection(sock);
      exit(EXIT_FAILURE);
   }

   fd_set rdfs;

   while(1)
   {
      int i = 0;
      FD_ZERO(&rdfs);

      FD_SET(STDIN_FILENO, &rdfs);

      FD_SET(sock, &rdfs);

      for(i = 0; i < actual; i++)
      {
         FD_SET(clients[i].sock, &rdfs);
      }

      if(select(max + 1, &rdfs, NULL, NULL, NULL) == -1)
      {
         perror("select()");
         exit(errno);
      }

      if(FD_ISSET(STDIN_FILENO, &rdfs))
      {

         break;
      }
      else if(FD_ISSET(sock, &rdfs))
      {

         SOCKADDR_IN csin = { 0 };
#ifdef WIN32
         int sinsize = sizeof csin;
#else
         socklen_t sinsize = sizeof csin;
#endif
         int csock = accept(sock, (SOCKADDR *)&csin, &sinsize);
         if(csock == SOCKET_ERROR)
         {
            perror("accept()");
            continue;
         }

         if(read_client(csock, buffer) <= 0)
         {
            closesocket(csock);
            continue;
         }

         buffer[strcspn(buffer, "\r\n")] = '\0';
         if(actual >= MAX_CLIENTS || buffer[0] == '\0' ||
            trouver_client_par_pseudo(clients, actual, buffer) >= 0)
         {
            write_client(csock, "Connexion refusee : pseudo invalide ou deja utilisé.");
            closesocket(csock);
            continue;
         }

         max = csock > max ? csock : max;

         Client c = {0};
         c.sock = csock;
         c.available = true;
         snprintf(c.name, sizeof c.name, "%s", buffer);
         c.bio[0] = '\0';
         clients[actual] = c;
         write_client(csock, "Connecte.");
         afficher_commandes(csock);
         actual++;
      }
      else
      {
         int i = 0;
         for(i = 0; i < actual; i++)
         {

            if(FD_ISSET(clients[i].sock, &rdfs))
            {
               Client client = clients[i];
               int c = read_client(clients[i].sock, buffer);

               if(c == 0)
               {
                  for(int g = 0; g < MAX_CLIENTS / 2; g++)
                  {
                     retirer_spectateur(&games[g], client.sock);
                  }

                  for(int g = 0; g < MAX_CLIENTS / 2; g++)
                  {
                     Game *game = &games[g];
                     if((game->active || game->awaiting_save ||
                         (game->suspended && game->resume_pending)) &&
                        (game->player1.sock == client.sock ||
                         game->player2.sock == client.sock))
                     {
                        SOCKET other_sock = game->player1.sock == client.sock
                           ? game->player2.sock : game->player1.sock;
                        for(int k = 0; k < actual; k++)
                        {
                           if(clients[k].sock == other_sock)
                           {
                              clients[k].available = true;
                              write_client(other_sock, game->awaiting_save
                                 ? "Demande de sauvegarde annulee : votre adversaire s'est deconnecte."
                                 : (game->resume_pending
                                    ? "Demande de reprise annulee : votre adversaire s'est deconnecte."
                                    : "Votre adversaire s'est deconnecte. La partie est interrompue; utilisez LIST MY GAMES pour la reprendre."));
                              break;
                           }
                        }
                        for(int k = 0; k < game->nombre_spectateurs; k++)
                        {
                           write_client(game->spectateurs[k],
                                        "Partie interrompue : un joueur s'est deconnecte.");
                        }
                        game->nombre_spectateurs = 0;
                        if(game->awaiting_save)
                        {
                           liberer_partie(game);
                        }
                        else if(game->active)
                        {
                           game->active = false;
                           game->suspended = true;
                           game->resume_pending = false;
                           game->resume_requester = 0;
                           if(game->player1.sock == client.sock)
                           {
                              game->player1.sock = INVALID_SOCKET;
                           }
                           else
                           {
                              game->player2.sock = INVALID_SOCKET;
                           }
                           if(!ecrire_etats_parties_en_cours(games))
                           {
                              fprintf(stderr, "Impossible de sauvegarder la partie interrompue %d\n",
                                      game->num_partie);
                              write_client(other_sock,
                                           "Erreur : la partie interrompue n'a pas pu etre sauvegardee sur le serveur.");
                           }
                        }
                        else
                        {
                           game->resume_pending = false;
                           game->resume_requester = 0;
                           game->player1.sock = INVALID_SOCKET;
                           game->player2.sock = INVALID_SOCKET;
                           if(!ecrire_etats_parties_en_cours(games))
                           {
                              fprintf(stderr, "Impossible de sauvegarder les parties recuperables\n");
                              write_client(other_sock,
                                           "Erreur : impossible de sauvegarder l'etat recuperable de la partie.");
                           }
                        }
                     }
                  }
                  if(client.invited_by[0] != '\0')
                  {
                     int inviter = trouver_client_par_pseudo(clients, actual, client.invited_by);
                     if(inviter >= 0)
                     {
                        clients[inviter].available = true;
                        write_client(clients[inviter].sock, "Invitation annulee : le joueur s'est deconnecte.");
                     }
                  }
                  for(int k = 0; k < actual; k++)
                  {
                     if(strcmp(clients[k].invited_by, client.name) == 0)
                     {
                        clients[k].invited_by[0] = '\0';
                        clients[k].available = true;
                        write_client(clients[k].sock, "Invitation annulee : le joueur s'est deconnecte.");
                     }
                  }
                  closesocket(clients[i].sock);
                  remove_client(clients, i, &actual);
               }
               else
               {
                  buffer[strcspn(buffer, "\r\n")] = '\0';
                  /* REVIEW et NEXT : choix puis parcours d'une partie sauvegardee. */
                  if(strcmp(buffer, "REVIEW") == 0)
                  {
                     client.review_selection_pending = false;
                     client.reviewing = false;
                     if(envoyer_liste_sauvegardes(client.sock,
                                                   &client.review_game_count))
                     {
                        client.review_selection_pending =
                           client.review_game_count > 0;
                     }
                  }
                  else if(strcmp(buffer, "NEXT") == 0)
                  {
                     if(client.reviewing)
                     {
                        afficher_coup_sauvegarde(
                           &client, client.review_game_index,
                           client.review_state_index + 1);
                     }
                     else
                     {
                        write_client(client.sock,
                                     "Aucune partie n'est en cours de relecture.");
                     }
                  }
                  else if(client.review_selection_pending)
                  {
                     size_t game_index;
                     if(!lire_numero_commande(buffer, &game_index) ||
                        game_index > client.review_game_count)
                     {
                        write_client(client.sock,
                                     "Numero invalide. Entrez un numero de la liste.");
                     }
                     else
                     {
                        client.review_selection_pending = false;
                        afficher_coup_sauvegarde(&client, game_index, 1);
                     }
                  }
                  /* OUI/NON : confirmation ou refus de sauvegarder la partie terminee. */
                  else if(strcmp(buffer, "OUI") == 0 || strcmp(buffer, "NON") == 0)
                  {
                     int game_index = trouver_sauvegarde_en_attente(games, client.sock);
                     if(game_index < 0)
                     {
                        write_client(client.sock, "Aucune demande de sauvegarde en attente.");
                     }
                     else
                     {
                        Game *game = &games[game_index];
                        bool save = strcmp(buffer, "OUI") == 0;
                        bool saved = save && !game->history_failed &&
                                     enregistrer_partie(game);
                        char message[BUF_SIZE];

                        if(saved)
                        {
                           snprintf(message, sizeof message,
                                    "%.900s a sauvegarde le jeu.",
                                    client.name);
                        }
                        else if(!save)
                        {
                           snprintf(message, sizeof message,
                                    "Partie abandonnee (non sauvegardee) par %.850s.",
                                    client.name);
                        }
                        else
                        {
                           snprintf(message, sizeof message,
                                    "Echec de la sauvegarde de la partie %d.",
                                    game->num_partie);
                        }

                        write_client(game->player1.sock, message);
                        write_client(game->player2.sock, message);
                        for(int k = 0; k < actual; k++)
                        {
                           if(clients[k].sock == game->player1.sock ||
                              clients[k].sock == game->player2.sock)
                           {
                              clients[k].available = true;
                           }
                        }
                        liberer_partie(game);
                     }
                  }
                  /* LIST PLAYERS : joueurs disponibles et joueurs en partie. */
                  else if(strcmp(buffer, "LIST PLAYERS") == 0 )
                  {
                     envoyer_liste_joueurs(clients, actual, games, client.sock);
                  }
                  /* LIST FRIENDS : amis et demandes recues. */
                  else if(strcmp(buffer, "LIST FRIENDS") == 0)
                  {
                     envoyer_liste_amis(&client);
                  }
                  /* ADD FRIEND : envoi d'une demande a un joueur connecte. */
                  else if(strncmp(buffer, "ADD FRIEND ", 11) == 0)
                  {
                     char username[BUF_SIZE];
                     int target_index;
                     if(sscanf(buffer + 11, "%1023s", username) != 1)
                     {
                        write_client(client.sock, "Usage : ADD FRIEND pseudo");
                        continue;
                     }

                     target_index = trouver_client_par_pseudo(
                        clients, actual, username);
                     if(target_index < 0)
                     {
                        write_client(client.sock,
                                     "Pseudo introuvable ou joueur deconnecte.");
                     }
                     else if(target_index == i)
                     {
                        write_client(client.sock,
                                     "Vous ne pouvez pas vous ajouter vous-meme.");
                     }
                     else if(sont_amis(client.name, username))
                     {
                        write_client(client.sock, "Vous etes deja amis.");
                     }
                     else if(!ajouter_demande_amitie(client.name, username))
                     {
                        write_client(client.sock,
                                     "Une demande d'amitie est deja en attente.");
                     }
                     else
                     {
                        snprintf(buffer, sizeof buffer,
                                 "%.450s vous demande en ami. Repondez : ACCEPT FRIEND %.450s",
                                 client.name, client.name);
                        write_client(clients[target_index].sock, buffer);
                        write_client(client.sock, "Demande d'amitie envoyee.");
                     }
                  }
                  /* REMOVE FRIEND : suppression du lien des deux cotes. */
                  else if(strncmp(buffer, "REMOVE FRIEND ", 14) == 0)
                  {
                     char username[BUF_SIZE];
                     if(sscanf(buffer + 14, "%1023s", username) != 1)
                     {
                        write_client(client.sock, "Usage : REMOVE FRIEND pseudo");
                        continue;
                     }
                     if(strcmp(username, client.name) == 0)
                     {
                        write_client(client.sock,
                                     "Vous ne pouvez pas vous supprimer de votre liste d'amis.");
                     }
                     else if(!supprimer_lien_amitie(client.name, username))
                     {
                        write_client(client.sock,
                                     "Cet utilisateur n'est pas dans votre liste d'amis, ou la suppression a echoue.");
                     }
                     else
                     {
                        snprintf(buffer, sizeof buffer,
                                 "%.900s a ete retire de votre liste d'amis.",
                                 username);
                        write_client(client.sock, buffer);

                        int friend_index = trouver_client_par_pseudo(
                           clients, actual, username);
                        if(friend_index >= 0)
                        {
                           snprintf(buffer, sizeof buffer,
                                    "%.900s vous a retire de sa liste d'amis.",
                                    client.name);
                           write_client(clients[friend_index].sock, buffer);
                        }
                     }
                  }
                  /* ACCEPT FRIEND : validation d'une demande recue. */
                  else if(strncmp(buffer, "ACCEPT FRIEND ", 14) == 0)
                  {
                     char username[BUF_SIZE];
                     int sender_index;
                     if(sscanf(buffer + 14, "%1023s", username) != 1)
                     {
                        write_client(client.sock, "Usage : ACCEPT FRIEND pseudo");
                        continue;
                     }

                     sender_index = trouver_client_par_pseudo(
                        clients, actual, username);
                     if(sender_index < 0)
                     {
                        write_client(client.sock,
                                     "Pseudo introuvable ou joueur deconnecte.");
                     }
                     else if(!accepter_demande_amitie(username, client.name))
                     {
                        write_client(client.sock,
                                     "Aucune demande d'amitie de ce joueur, ou echec de sauvegarde.");
                     }
                     else
                     {
                        snprintf(buffer, sizeof buffer,
                                 "Vous etes maintenant amis avec %.900s.",
                                 username);
                        write_client(client.sock, buffer);
                        snprintf(buffer, sizeof buffer,
                                 "%.900s a accepte votre demande d'amitie.",
                                 client.name);
                        write_client(clients[sender_index].sock, buffer);
                     }
                  }
                  /* REFUSE FRIEND : retrait d'une demande recue. */
                  else if(strncmp(buffer, "REFUSE FRIEND ", 14) == 0)
                  {
                     char username[BUF_SIZE];
                     if(sscanf(buffer + 14, "%1023s", username) != 1)
                     {
                        write_client(client.sock, "Usage : REFUSE FRIEND pseudo");
                        continue;
                     }

                     if(!refuser_demande_amitie(username, client.name))
                     {
                        write_client(client.sock,
                                     "Aucune demande d'amitie de ce joueur.");
                     }
                     else
                     {
                        write_client(client.sock,
                                     "Demande d'amitie refusee.");
                        int sender_index = trouver_client_par_pseudo(
                           clients, actual, username);
                        if(sender_index >= 0)
                        {
                           snprintf(buffer, sizeof buffer,
                                    "%.900s a refuse votre demande d'amitie.",
                                    client.name);
                           write_client(clients[sender_index].sock, buffer);
                        }
                     }
                  }
                  /* LIST MY GAMES : parties interrompues du joueur. */
                  else if(strcmp(buffer, "LIST MY GAMES") == 0)
                  {
                     envoyer_mes_parties_en_cours(games, client.name,
                                                   client.sock);
                  }
                  /* RESUME et reponse de l'adversaire : reprise d'une partie. */
                  else if(strncmp(buffer, "RESUME ", 7) == 0)
                  {
                     size_t requested_game;
                     if(!lire_numero_commande(buffer + 7, &requested_game) ||
                        requested_game > INT_MAX)
                     {
                        write_client(client.sock,
                                     "Usage : RESUME numero_partie");
                        continue;
                     }

                     int game_index = trouver_partie_suspendue_par_numero(
                        games, (int)requested_game);
                     if(game_index < 0)
                     {
                        write_client(client.sock,
                                     "Partie recuperable introuvable.");
                        continue;
                     }

                     Game *game = &games[game_index];
                     if(game->resume_pending)
                     {
                        write_client(client.sock,
                                     "Une demande de reprise est deja en cours pour cette partie.");
                        continue;
                     }

                     int requester = strcmp(game->player1.name,
                                            client.name) == 0 ? 1 :
                        (strcmp(game->player2.name, client.name) == 0 ? 2 : 0);
                     if(requester == 0)
                     {
                        write_client(client.sock,
                                     "Cette partie ne vous appartient pas.");
                        continue;
                     }

                     const char *opponent_name = requester == 1
                        ? game->player2.name : game->player1.name;
                     int opponent_index = trouver_client_par_pseudo(
                        clients, actual, opponent_name);
                     if(opponent_index < 0)
                     {
                        write_client(client.sock,
                                     "Votre adversaire n'est pas connecte. Reessayez plus tard.");
                        continue;
                     }
                     if(!client.available ||
                        !clients[opponent_index].available)
                     {
                        write_client(client.sock,
                                     "Vous ou votre adversaire etes deja occupes.");
                        continue;
                     }

                     game->player1.sock = requester == 1
                        ? client.sock : clients[opponent_index].sock;
                     game->player2.sock = requester == 2
                        ? client.sock : clients[opponent_index].sock;
                     game->resume_pending = true;
                     game->resume_requester = requester;
                     clients[i].available = false;
                     clients[opponent_index].available = false;
                     snprintf(buffer, sizeof buffer,
                              "DEMANDE DE REPRISE de %.400s pour la partie %d. Repondez ACCEPT RESUME %d ou REFUSE RESUME %d.",
                              client.name, game->num_partie, game->num_partie,
                              game->num_partie);
                     write_client(clients[opponent_index].sock, buffer);
                     snprintf(buffer, sizeof buffer,
                              "Demande de reprise envoyee a %.400s.",
                              opponent_name);
                     write_client(client.sock, buffer);
                  }
                  else if(strncmp(buffer, "ACCEPT RESUME ", 14) == 0 ||
                          strncmp(buffer, "REFUSE RESUME ", 14) == 0)
                  {
                     bool accept_resume = strncmp(buffer, "ACCEPT ", 7) == 0;
                     size_t requested_game;
                     if(!lire_numero_commande(buffer + 14, &requested_game) ||
                        requested_game > INT_MAX)
                     {
                        write_client(client.sock,
                                     accept_resume
                                       ? "Usage : ACCEPT RESUME numero_partie"
                                       : "Usage : REFUSE RESUME numero_partie");
                        continue;
                     }

                     int game_index = trouver_partie_suspendue_par_numero(
                        games, (int)requested_game);
                     if(game_index < 0 || !games[game_index].resume_pending)
                     {
                        write_client(client.sock,
                                     "Aucune demande de reprise en attente pour cette partie.");
                        continue;
                     }

                     Game *game = &games[game_index];
                     int responder = strcmp(game->player1.name,
                                            client.name) == 0 ? 1 :
                        (strcmp(game->player2.name, client.name) == 0 ? 2 : 0);
                     if(responder == 0 ||
                        responder == game->resume_requester ||
                        (responder == 1 && game->player1.sock != client.sock) ||
                        (responder == 2 && game->player2.sock != client.sock))
                     {
                        write_client(client.sock,
                                     "Seul l'adversaire concerne peut repondre a cette demande.");
                        continue;
                     }

                     int requester_index = trouver_client_par_pseudo(
                        clients, actual,
                        game->resume_requester == 1
                           ? game->player1.name : game->player2.name);
                     if(requester_index < 0)
                     {
                        game->resume_pending = false;
                        game->resume_requester = 0;
                        game->player1.sock = INVALID_SOCKET;
                        game->player2.sock = INVALID_SOCKET;
                        clients[i].available = true;
                        write_client(client.sock,
                                     "La personne ayant demande la reprise est deconnectee. Reessayez plus tard.");
                        if(!ecrire_etats_parties_en_cours(games))
                        {
                           write_client(client.sock,
                                        "Erreur : impossible de sauvegarder l'etat recuperable de la partie.");
                        }
                        continue;
                     }

                     if(accept_resume)
                     {
                        game->resume_pending = false;
                        game->resume_requester = 0;
                        game->suspended = false;
                        game->active = true;
                        clients[requester_index].available = false;
                        clients[i].available = false;
                        snprintf(buffer, sizeof buffer,
                                 "Reprise de la partie %d acceptee.",
                                 game->num_partie);
                        write_client(game->player1.sock, buffer);
                        write_client(game->player2.sock, buffer);
                        if(game->privacy_pending)
                        {
                           write_client(game->player1.sock,
                                        "Choisissez la visibilite de la partie : PRIVATE ou PUBLIC.");
                           write_client(game->player2.sock,
                                        "Attendez que le createur choisisse la visibilite de la partie.");
                        }
                        else
                        {
                           envoyer_etat_partie(game);
                        }
                        if(!ecrire_etats_parties_en_cours(games))
                        {
                           write_client(game->player1.sock,
                                        "Erreur : impossible de sauvegarder l'etat recuperable de la partie.");
                           write_client(game->player2.sock,
                                        "Erreur : impossible de sauvegarder l'etat recuperable de la partie.");
                        }
                     }
                     else
                     {
                        game->resume_pending = false;
                        game->resume_requester = 0;
                        game->player1.sock = INVALID_SOCKET;
                        game->player2.sock = INVALID_SOCKET;
                        clients[requester_index].available = true;
                        clients[i].available = true;
                        write_client(clients[requester_index].sock,
                                     "Votre demande de reprise a ete refusee. La partie reste recuperable.");
                        write_client(client.sock,
                                     "Demande de reprise refusee. La partie reste recuperable.");
                     }
                  }
                  /* LIST GAMES : parties actuellement actives. */
                  else if(strcmp(buffer, "LIST GAMES") == 0)
                  {
                     envoyer_liste_parties(games, client.sock);
                  }
                  /* PRIVATE/PUBLIC : choix de visibilite par le createur. */
                  else if(strcmp(buffer, "PRIVATE") == 0 ||
                          strcmp(buffer, "PUBLIC") == 0)
                  {
                     int game_index = trouver_partie_par_socket(
                        games, client.sock);
                     if(game_index < 0 || !games[game_index].privacy_pending)
                     {
                        write_client(client.sock,
                                     "Aucune partie n'attend de choix de visibilite.");
                     }
                     else if(games[game_index].player1.sock != client.sock)
                     {
                        write_client(client.sock,
                                     "Seul le createur de la partie peut choisir sa visibilite.");
                     }
                     else
                     {
                        Game *game = &games[game_index];
                        game->private_game = strcmp(buffer, "PRIVATE") == 0;
                        game->privacy_pending = false;
                        write_client(game->player1.sock, game->private_game
                           ? "La partie est privee : seuls vos amis ou ceux de votre adversaire peuvent la regarder."
                           : "La partie est publique : tous les joueurs peuvent la regarder.");
                        write_client(game->player2.sock, game->private_game
                           ? "La partie est privee : seuls les amis d'au moins un joueur peuvent la regarder."
                           : "La partie est publique : tous les joueurs peuvent la regarder.");
                        if(!ecrire_etats_parties_en_cours(games))
                        {
                           write_client(game->player1.sock,
                                        "Erreur : impossible de sauvegarder l'etat recuperable de la partie.");
                           write_client(game->player2.sock,
                                        "Erreur : impossible de sauvegarder l'etat recuperable de la partie.");
                        }
                        envoyer_etat_partie(game);
                     }
                  }
                  /* WATCH et STOP WATCH : abonnement aux mises a jour d'une partie. */
                  else if(strncmp(buffer, "WATCH ", 6) == 0)
                  {
                     int numero_partie = 0;
                     char caractere_supplementaire;
                     if(sscanf(buffer + 6, "%d %c", &numero_partie,
                               &caractere_supplementaire) != 1 || numero_partie <= 0)
                     {
                        write_client(client.sock, "Usage : WATCH numero_partie");
                        continue;
                     }

                     if(trouver_partie_par_socket(games, client.sock) >= 0)
                     {
                        write_client(client.sock, "Vous ne pouvez pas regarder une partie pendant que vous jouez.");
                        continue;
                     }

                     int game_index = trouver_partie_par_numero(games, numero_partie);
                     if(game_index < 0)
                     {
                        write_client(client.sock, "Partie introuvable ou terminee.");
                     }
                     else if(games[game_index].privacy_pending)
                     {
                        write_client(client.sock,
                                     "La visibilite de cette partie n'a pas encore ete choisie.");
                     }
                     else if(games[game_index].private_game &&
                             !peut_regarder_partie_privee(
                                &client, &games[game_index]))
                     {
                        snprintf(buffer, sizeof buffer,
                                 "Vous n'etes pas dans la liste d'amis de %.250s et %.250s. Envoyez une demande avec la commande ADD FRIEND pseudo.",
                                 games[game_index].player1.name,
                                 games[game_index].player2.name);
                        write_client(client.sock, buffer);
                     }
                     else if(!ajouter_spectateur(&games[game_index], client.sock))
                     {
                        write_client(client.sock, "La partie a atteint sa limite de spectateurs.");
                     }
                     else
                     {
                        Game *game = &games[game_index];
                        snprintf(buffer, sizeof buffer,
                                 "Vous regardez maintenant la partie %d.", numero_partie);
                        write_client(client.sock, buffer);
                        envoyer_etat_spectateur(game, client.sock);
                     }
                  }
                  else if(strcmp(buffer, "STOP WATCH") == 0)
                  {
                     int stopped_watching = 0;
                     for(int g = 0; g < MAX_CLIENTS / 2; g++)
                     {
                        int previous_count = games[g].nombre_spectateurs;
                        retirer_spectateur(&games[g], client.sock);
                        if(games[g].nombre_spectateurs < previous_count)
                        {
                           stopped_watching++;
                        }
                     }

                     write_client(client.sock, stopped_watching > 0
                        ? "Vous ne regardez plus aucune partie."
                        : "Vous ne regardez aucune partie.");
                  }
                  /* CHALLENGE puis ACCEPT/REFUSE : creation d'une nouvelle partie. */
                  else if(strncmp(buffer, "CHALLENGE ", 10) == 0)
                  {
                     char username[BUF_SIZE];
                     int target_index;
                     if(sscanf(buffer + 10, "%1023s", username) != 1)
                     {
                        write_client(client.sock, "Usage : CHALLENGE pseudo");
                        continue;
                     }

                     target_index = trouver_client_par_pseudo(clients, actual, username);
                     if(target_index < 0)
                     {
                        write_client(client.sock, "Pseudo introuvable.");
                     }
                     else if(target_index == i)
                     {
                        write_client(client.sock, "Vous ne pouvez pas vous envoyer un message a vous-meme.");
                     }
                     else if(!client.available || !clients[target_index].available)
                     {
                        write_client(client.sock, "Vous ou ce joueur etes deja en partie ou avez une invitation en attente.");
                     }
                     else
                     {
                        clients[i].available = false;
                        clients[target_index].available = false;
                        snprintf(clients[target_index].invited_by,
                                 sizeof clients[target_index].invited_by, "%s", client.name);
                        snprintf(buffer, sizeof buffer,
                                 "INVITATION de %.900s. Repondez ACCEPT ou REFUSE.",
                                 client.name);
                        write_client(clients[target_index].sock, buffer);
                        snprintf(buffer, sizeof buffer, "Invitation envoyee a %.950s.", username);
                        write_client(client.sock, buffer);
                     }
                  }
                  else if(strcmp(buffer, "ACCEPT") == 0 || strcmp(buffer, "REFUSE") == 0)
                  {
                     bool accept = strcmp(buffer, "ACCEPT") == 0;
                     if(client.invited_by[0] == '\0')
                     {
                        write_client(client.sock, "Aucune invitation en attente.");
                        continue;
                     }

                     int inviter_index = trouver_client_par_pseudo(
                        clients, actual, client.invited_by);
                     if(inviter_index < 0)
                     {
                        clients[i].invited_by[0] = '\0';
                        clients[i].available = true;
                        write_client(client.sock, "L'invitation n'est plus valide.");
                        continue;
                     }

                     if(!accept)
                     {
                        clients[inviter_index].available = true;
                        clients[i].available = true;
                        clients[i].invited_by[0] = '\0';
                        write_client(clients[inviter_index].sock, "Invitation refusee.");
                        write_client(client.sock, "Invitation refusee.");
                        continue;
                     }

                     int game_index = trouver_partie_libre(games);
                     if(game_index < 0)
                     {
                        clients[inviter_index].available = true;
                        clients[i].available = true;
                        clients[i].invited_by[0] = '\0';
                        write_client(clients[inviter_index].sock, "Impossible de creer une partie : toutes les parties sont occupees.");
                        write_client(client.sock, "Impossible de creer une partie : toutes les parties sont occupees.");
                        continue;
                     }

                     Game *game = &games[game_index];
                     memset(game, 0, sizeof *game);
                     game->active = true;
                     game->num_partie = next_game_id++;
                     game->player1 = clients[inviter_index];
                     game->player2 = clients[i];
                     game->current_player = rand() > RAND_MAX / 2 ? 2 : 1;
                     clients[i].invited_by[0] = '\0';
                     demarrer_partie(game->board, game->points);
                     snprintf(buffer, sizeof buffer, "Partie %d creee contre %.900s.",
                              game->num_partie, client.name);
                     write_client(clients[inviter_index].sock, buffer);
                     snprintf(buffer, sizeof buffer, "Partie %d creee contre %.900s.",
                              game->num_partie, clients[inviter_index].name);
                     write_client(client.sock, buffer);
                     write_client(game->player2.sock,
                                  "Attendez que le createur choisisse la visibilite de la partie.");
                     partie_privee(game->player1.sock, game);
                     if(!ecrire_etats_parties_en_cours(games))
                     {
                        write_client(game->player1.sock,
                                     "Erreur : la partie ne pourra pas etre restauree apres un arret du serveur.");
                        write_client(game->player2.sock,
                                     "Erreur : la partie ne pourra pas etre restauree apres un arret du serveur.");
                     }
                  }
                  /* MOVE : validation du tour, application du coup et sauvegarde. */
                  else if(strncmp(buffer, "MOVE ", 5) == 0)
                  {
                     int game_index = trouver_partie_par_socket(games, client.sock);
                     int pit = 0;
                     if(game_index < 0)
                     {
                        write_client(client.sock, "Vous n'etes pas dans une partie.");
                        continue;
                     }
                     if(sscanf(buffer + 5, "%d", &pit) != 1 || pit < 1 || pit > 6)
                     {
                        write_client(client.sock, "Usage : MOVE numero_de_case (1 a 6)");
                        continue;
                     }

                     Game *game = &games[game_index];
                     if(game->privacy_pending)
                     {
                        write_client(client.sock,
                                     "Attendez le choix de visibilite avant de jouer.");
                        continue;
                     }
                     int player = game->player1.sock == client.sock ? 1 : 2;
                     if(player != game->current_player)
                     {
                        write_client(client.sock, "Ce n'est pas votre tour.");
                        continue;
                     }
                     if(!verif_end_game(game->board, game->points, game->current_player))
                     {
                        envoyer_fin_partie(game);
                        demander_sauvegarde(games, game, clients, actual);
                        continue;
                     }
                     if(!traiter_coup(game->board, game->points, player, pit))
                     {
                        write_client(client.sock, "Coup invalide.");
                        continue;
                     }

                     game->current_player = game->current_player == 1 ? 2 : 1;
                     bool game_continues = verif_end_game(
                        game->board, game->points, game->current_player);
                     if(!memoriser_etat_partie(game))
                     {
                        game->history_failed = true;
                        write_client(game->player1.sock,
                                     "Erreur : l'historique de la partie ne peut pas etre sauvegarde.");
                        write_client(game->player2.sock,
                                     "Erreur : l'historique de la partie ne peut pas etre sauvegarde.");
                     }
                     if(!ecrire_etats_parties_en_cours(games))
                     {
                        write_client(game->player1.sock,
                                     "Erreur : impossible de sauvegarder l'etat recuperable de la partie.");
                        write_client(game->player2.sock,
                                     "Erreur : impossible de sauvegarder l'etat recuperable de la partie.");
                     }
                     if(!game_continues)
                     {
                        envoyer_fin_partie(game);
                        demander_sauvegarde(games, game, clients, actual);
                     }
                     else
                     {
                        envoyer_etat_partie(game);
                     }
                  }
                  /* END : fin volontaire et demande de sauvegarde. */
                  else if(strcmp(buffer, "END") == 0)
                  {
                     int game_index = trouver_partie_par_socket(games, client.sock);
                     if(game_index >= 0)
                     {
                        demander_sauvegarde(games, &games[game_index],
                                             clients, actual);
                     }
                     else
                     {
                        write_client(client.sock, "Vous n'etes pas dans une partie.");
                     }
                  }
                  /* SEND PUBLIC/PRIVATE : messages entre joueurs. */
                  else if (strncmp(buffer, "SEND PUBLIC", 11) == 0){
                     send_message_to_all_clients(clients, client, actual, buffer + 12, 0);
                  }
                  else if (strncmp(buffer, "SEND PRIVATE", 12)== 0){
                     char username[BUF_SIZE];
                     char message[BUF_SIZE];
                     int target_index;
                     int username_length = 0;
                     const char *private_text;
                     if(sscanf(buffer + 13, "%1023s%n", username, &username_length) != 1)
                     {
                        write_client(client.sock, "Usage : SEND PRIVATE pseudo message");
                        continue;
                     }

                     private_text = buffer + 13 + username_length;
                     while(*private_text == ' ' || *private_text == '\t')
                     {
                        private_text++;
                     }
                     if(*private_text == '\0')
                     {
                        write_client(client.sock, "Usage : SEND PRIVATE pseudo message");
                        continue;
                     }

                     target_index = trouver_client_par_pseudo(clients, actual, username);
                     if(target_index < 0)
                     {
                        write_client(client.sock, "Pseudo introuvable.");
                     }
                     else if(target_index == i)
                     {
                        write_client(client.sock, "Vous ne pouvez pas envoyer de message vous-meme.");
                     }
                     else
                     {
                        snprintf(message, sizeof message, "%s (prive) : %s", client.name, private_text);
                        write_client(clients[target_index].sock, message);
                     }
                  }
                  /* HELP et commandes de biographie. */
                  else if(strcmp(buffer, "HELP") == 0)
                  {
                     afficher_commandes(client.sock);
                  }
                  else if(strncmp(buffer, "WRITE BIO ", 10) == 0)
                  {
                     const char *bio_text = buffer + 10;
                     while(*bio_text == ' ' || *bio_text == '\t')
                     {
                        bio_text++;
                     }
                     if(*bio_text == '\0')
                     {
                        write_client(client.sock, "Usage : WRITE BIO votre_biographie");
                        continue;
                     }
                     ecrire_biographie(client.sock, clients, actual, bio_text);
                     strncpy(clients[i].bio, bio_text, sizeof clients[i].bio - 1);
                  }
                  else if(strncmp(buffer, "READ BIO ", 9) == 0)
                  {
                     char username[BUF_SIZE];
                     if(sscanf(buffer + 9, "%1023s", username) != 1)
                     {
                        write_client(client.sock, "Usage : READ BIO pseudo");
                        continue;
                     }
                     lire_biographie(client.sock, clients, actual, username);
                  }
                  else {
                     write_client(client.sock,
                                  "Commande inconnue. Tapez HELP pour afficher les commandes disponibles.");
                  }

               }
               clients[i].review_selection_pending = client.review_selection_pending;
               clients[i].reviewing = client.reviewing;
               clients[i].review_game_index = client.review_game_index;
               clients[i].review_game_count = client.review_game_count;
               clients[i].review_state_index = client.review_state_index;
               break;
            }
         }

      }
   }

   if(!ecrire_etats_parties_en_cours(games))
   {
      fprintf(stderr, "Les parties en cours n'ont pas toutes ete sauvegardees a l'arret.\n");
   }
   liberer_amitiees();
   for(int g = 0; g < MAX_CLIENTS / 2; g++)
   {
      liberer_partie(&games[g]);
   }
   clear_clients(clients, actual);
   end_connection(sock);
}

int main(int argc, char **argv)
{
   srand((unsigned int)time(NULL));

   init();

   app();

   end();

   return EXIT_SUCCESS;
}
