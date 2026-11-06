#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>

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
      if(!games[i].active)
      {
         return i;
      }
   }

   return -1;
}

static void terminer_partie(Game *game, Client *clients, int actual, const char *message)
{
   for(int i = 0; i < actual; i++)
   {
      if(clients[i].sock == game->player1.sock ||
         clients[i].sock == game->player2.sock)
      {
         clients[i].available = true;
         clients[i].invited_by[0] = '\0';
         write_client(clients[i].sock, message);
      }
   }

   game->active = false;
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
                           "SAVE              - Sauvegarde la partie en cours\n"
                           "REVIEW            - Revoit une partie sauvegardée\n";

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
                     if(games[g].active &&
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
                              write_client(other_sock, "Votre adversaire s'est deconnecte. Vous etes disponible.");
                              break;
                           }
                        }
                        games[g].active = false;
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
                  if(strcmp(buffer, "LIST PLAYERS") == 0 )
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
                        terminer_partie(game, clients, actual, "Partie terminee.");
                        continue;
                     }
                     if(!traiter_coup(game->board, game->points, player, pit))
                     {
                        write_client(client.sock, "Coup invalide.");
                        continue;
                     }

                     game->current_player = game->current_player == 1 ? 2 : 1;
                     if(!verif_end_game(game->board, game->points, game->current_player))
                     {
                        terminer_partie(game, clients, actual, "Partie terminee.");
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
                        terminer_partie(&games[game_index], clients, actual,
                                        "Partie terminee.");
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
               break;
            }
         }


      }
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
