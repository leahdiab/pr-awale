#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "server2.h"
#include "client2.h"
#include "awale.h"

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

// void afficher_etat_jeu(const int board[12], const int points[2], int current_player)
// {
//    int i = 0;

//    printf("Plateau :\n\n");
//    for(i = 11; i >= 6; i--)
//    {
//       printf("%d  | ", board[i]);
//    }
//    printf("\n");
//    for(i = 0; i < 6; i++)
//    {
//       printf("%d  | ", board[i]);
//    }
//    printf("\n\nPoints : joueur 1 = %d, joueur 2 = %d\n", points[0], points[1]);
//    printf("Joueur courant : %d\n", current_player);
// }

void demarrer_partie(int board[12], int points[2])
{
   int i = 0;
   for(i = 0; i < 12; i++)
   {
      board[i] = 4;
   }
   points[0] = 0;
   points[1] = 0;
}

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

static void liberer_partie(Game *game)
{
   free(game->history);
   memset(game, 0, sizeof *game);
}

static void envoyer_liste_joueurs(Client *clients, int actual, SOCKET requester)
{
   char message[BUF_SIZE] = "Joueurs disponibles :";
   int count = 0;

   for(int i = 0; i < actual; i++)
   {
      if(clients[i].available && clients[i].sock != requester)
      {
         size_t used = strlen(message);
         int written = snprintf(message + used, sizeof message - used, "\n%s", clients[i].name);
         if(written < 0 || (size_t)written >= sizeof message - used)
         {
            break;
         }
         count++;
      }
   }

   if(count == 0)
   {
      strncat(message, "\nAucun joueur disponible.", sizeof message - strlen(message) - 1);
   }
   else
   {
      strncat(message, "\nPour inviter un joueur : INVITE pseudo",
              sizeof message - strlen(message) - 1);
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

static void envoyer_etat_partie(const Game *game)
{
   const char *username = game->current_player == 1
      ? game->player1.name : game->player2.name;

   envoyer_etat_jeu(game->player1.sock, game->board, game->points, username,
                    game->player2.name, game->player1.name);
   envoyer_etat_jeu(game->player2.sock, game->board, game->points, username,
                    game->player2.name, game->player1.name);
}

static int trouver_partie_libre(Game *games)
{
   for(int i = 0; i < MAX_CLIENTS / 2; i++)
   {
      if(!games[i].active && !games[i].awaiting_save)
      {
         return i;
      }
   }

   return -1;
}

static void demander_sauvegarde(Game *game, Client *clients, int actual)
{
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

   game->active = false;
   game->awaiting_save = true;
}

static void afficher_commandes(SOCKET sock){
   char buffer[BUF_SIZE] = "           Commandes disponibles :\n"
                           "HELP              - Affiche la liste des commandes disponibles\n"
                           "LIST PLAYERS      - Affiche la liste des joueurs disponibles\n"
                           "INVITE pseudo     - Invite un joueur à une partie\n"
                           "ACCEPT            - Accepte une invitation\n"
                           "REFUSE            - Refuse une invitation\n"
                           "MOVE n            - Joue un coup (1 à 6)\n"
                           "END               - Termine la partie en cours\n"
                           "SEND public       - Envoie un message à tous les joueurs connectés\n"
                           "SEND private pseudo - Envoie un message privé à un joueur\n"
                           "LIST GAMES        - Affiche la liste des parties en cours\n"
                           "WATCH game_id     - Regarde une partie en cours\n"
                           "WRITE BIO         - Ecrit ou modifie ta biographie\n"
                           "READ BIO pseudo   - Lit la biographie d'un joueur\n"
                           "OUI / NON         - Repond a une demande de sauvegarde\n"
                           "REVIEW            - Affiche les parties sauvegardees\n"
                           "NEXT              - Affiche le prochain coup pendant REVIEW\n";

   write_client(sock, buffer);
}


static void app(void)
{
   SOCKET sock = init_connection();
   char buffer[BUF_SIZE];
   /* the index for the array */
   int actual = 0;
   int max = sock;
   /* an array for all clients */
   Client clients[MAX_CLIENTS] = {0};
   Game games[MAX_CLIENTS / 2] = {0};
   int next_game_id = 1;

   fd_set rdfs;

   while(1)
   {
      int i = 0;
      FD_ZERO(&rdfs);

      /* add STDIN_FILENO */
      FD_SET(STDIN_FILENO, &rdfs);

      /* add the connection socket */
      FD_SET(sock, &rdfs);

      /* add socket of each client */
      for(i = 0; i < actual; i++)
      {
         FD_SET(clients[i].sock, &rdfs);
      }

      if(select(max + 1, &rdfs, NULL, NULL, NULL) == -1)
      {
         perror("select()");
         exit(errno);
      }

      /* something from standard input : i.e keyboard */
      if(FD_ISSET(STDIN_FILENO, &rdfs))
      {
         /* stop process when type on keyboard */
         break;
      }
      else if(FD_ISSET(sock, &rdfs))
      {
         /* new client */
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

         /* after connecting the client sends its name */
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

         /* what is the new maximum fd ? */
         max = csock > max ? csock : max;

         Client c = {0};
         c.sock = csock;
         c.available = true;
         snprintf(c.name, sizeof c.name, "%s", buffer);

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
            /* a client is talking */
            if(FD_ISSET(clients[i].sock, &rdfs))
            {
               Client client = clients[i];
               int c = read_client(clients[i].sock, buffer);
               /* client disconnected */
               if(c == 0)
               {
                  for(int g = 0; g < MAX_CLIENTS / 2; g++)
                  {
                     if((games[g].active || games[g].awaiting_save) &&
                        (games[g].player1.sock == client.sock ||
                         games[g].player2.sock == client.sock))
                     {
                        SOCKET other_sock = games[g].player1.sock == client.sock
                           ? games[g].player2.sock : games[g].player1.sock;
                        for(int k = 0; k < actual; k++)
                        {
                           if(clients[k].sock == other_sock)
                           {
                              clients[k].available = true;
                              write_client(other_sock, games[g].awaiting_save
                                 ? "Demande de sauvegarde annulee : votre adversaire s'est deconnecte."
                                 : "Votre adversaire s'est deconnecte. Vous etes disponible.");
                              break;
                           }
                        }
                        liberer_partie(&games[g]);
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
                  else if(strcmp(buffer, "LIST PLAYERS") == 0 )
                  {
                     envoyer_liste_joueurs(clients, actual, client.sock);
                  }
                  else if(strcmp(buffer, "LIST GAMES") == 0)
                  {
                     envoyer_liste_parties(games, client.sock);
                  }
                  else if(strncmp(buffer, "INVITE ", 7) == 0)
                  {
                     char username[BUF_SIZE];
                     int target_index;
                     if(sscanf(buffer + 7, "%1023s", username) != 1)
                     {
                        write_client(client.sock, "Usage : INVITE pseudo");
                        continue;
                     }

                     target_index = trouver_client_par_pseudo(clients, actual, username);
                     if(target_index < 0)
                     {
                        write_client(client.sock, "Pseudo introuvable.");
                     }
                     else if(target_index == i)
                     {
                        write_client(client.sock, "Vous ne pouvez pas vous inviter vous-meme.");
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
                     envoyer_etat_partie(game);
                  }
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
                     int player = game->player1.sock == client.sock ? 1 : 2;
                     if(player != game->current_player)
                     {
                        write_client(client.sock, "Ce n'est pas votre tour.");
                        continue;
                     }
                     if(!verif_end_game(game->board, game->points, game->current_player))
                     {
                        demander_sauvegarde(game, clients, actual);
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
                     if(!game_continues)
                     {
                        demander_sauvegarde(game, clients, actual);
                     }
                     else
                     {
                        envoyer_etat_partie(game);
                     }
                  }
                  else if(strcmp(buffer, "END") == 0)
                  {
                     int game_index = trouver_partie_par_socket(games, client.sock);
                     if(game_index >= 0)
                     {
                        demander_sauvegarde(&games[game_index], clients, actual);
                     }
                     else
                     {
                        write_client(client.sock, "Vous n'etes pas dans une partie.");
                     }
                  }
                  else if (strncmp(buffer, "SEND", 4) == 0){
                     send_message_to_all_clients(clients, client, actual, buffer + 5, 0);
                  }
                  else if(strcmp(buffer, "HELP") == 0)
                  {
                     afficher_commandes(client.sock);
                  }
                  else {
                     write_client(client.sock, "Commande inconnue. Utilisez LIST, INVITE pseudo, ACCEPT, REFUSE, MOVE n ou END.");
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

   for(int g = 0; g < MAX_CLIENTS / 2; g++)
   {
      liberer_partie(&games[g]);
   }
   clear_clients(clients, actual);
   end_connection(sock);
}

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
   /* we remove the client in the array */
   memmove(clients + to_remove, clients + to_remove + 1, (*actual - to_remove - 1) * sizeof(Client));
   /* number client - 1 */
   (*actual)--;
}

static void send_message_to_all_clients(Client *clients, Client sender, int actual, const char *buffer, char from_server)
{
   int i = 0;
   char message[BUF_SIZE];
   message[0] = 0;
   for(i = 0; i < actual; i++)
   {
      /* we don't send message to the sender */
      if(sender.sock != clients[i].sock)
      {
         if(from_server == 0)
         {
            strncpy(message, sender.name, BUF_SIZE - 1);
            strncat(message, " : ", sizeof message - strlen(message) - 1);
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

   if(sock == INVALID_SOCKET)
   {
      perror("socket()");
      exit(errno);
   }

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
      /* if recv error we disonnect the client */
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

int main(int argc, char **argv)
{
   srand((unsigned int)time(NULL));

   init();

   app();

   end();

   return EXIT_SUCCESS;
}
