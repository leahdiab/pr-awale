#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

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

void afficher_etat_jeu(const int board[12], const int points[2], int current_player)
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

void envoyer_etat_jeu(SOCKET sock, const int board[12], const int points[2], int current_player)
{
   char buffer[BUF_SIZE];
   snprintf(buffer, BUF_SIZE, "Plateau :\n\n");
   for(int i = 11; i >= 6; i--)
   {
      snprintf(buffer + strlen(buffer), BUF_SIZE - strlen(buffer), "%d  | ", board[i]);
   }
   snprintf(buffer + strlen(buffer), BUF_SIZE - strlen(buffer), "\n");
   for(int i = 0; i < 6; i++)
   {
      snprintf(buffer + strlen(buffer), BUF_SIZE - strlen(buffer), "%d  | ", board[i]);
   }
   snprintf(buffer + strlen(buffer), BUF_SIZE - strlen(buffer), "\n\nPoints : joueur 1 = %d, joueur 2 = %d\n", points[0], points[1]);
   snprintf(buffer + strlen(buffer), BUF_SIZE - strlen(buffer), "Joueur courant : %d\n", current_player);

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
   Client clients[MAX_CLIENTS];

   /*
   int board[12] = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
   int points[2] = {0, 0};
   int famine_board[12] = {1};
   int famine_points[2] = {0, 0};

   printf("Test local des fonctions Awalé\n");
   awale_afficher_etat_jeu(board, points, 1);
   // make_move(board, points, 1, 1);
   // awale_afficher_etat_jeu(board, points, 2);
   // if(verif_end_game(board, points, 2)) {
   //    printf("Test terminé : la partie peut continuer.\n");
   // }
   // printf("Test de fin par famine :\n");
   // if(!verif_end_game(famine_board, famine_points, 1)) {
   //    awale_afficher_etat_jeu(famine_board, famine_points, 1);
   // }
   */

   fd_set rdfs;

   int board[12];
   int points[2];
   int current_player;
   bool coup_valide = true;

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
         size_t sinsize = sizeof csin;
         int csock = accept(sock, (SOCKADDR *)&csin, &sinsize);
         if(csock == SOCKET_ERROR)
         {
            perror("accept()");
            continue;
         }

         /* after connecting the client sends its name */
         if(read_client(csock, buffer) == -1)
         {
            /* disconnected */
            continue;
         }

         /* what is the new maximum fd ? */
         max = csock > max ? csock : max;

         FD_SET(csock, &rdfs);

         // si on a déjà deux clients connectés, on refuse la connexion
         if (actual >= 2)
         {
            /* Refuser ou fermer la connexion : la partie a déjà deux joueurs. */
            closesocket(csock);
            continue;
         }

         // on ajoute le nouveau client à la liste des clients avec son nom et son numéro de joueur
         Client c = {0};
         c.sock = csock;
         c.player = actual + 1;  /* premier client = 1, deuxième = 2 */
         snprintf(c.name, sizeof c.name, "%s", buffer);

         clients[actual] = c;
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
                  closesocket(clients[i].sock);
                  remove_client(clients, i, &actual);
                  strncpy(buffer, client.name, BUF_SIZE - 1);
                  strncat(buffer, " disconnected !", BUF_SIZE - strlen(buffer) - 1);
                  send_message_to_all_clients(clients, client, actual, buffer, 1);
               }
               else
               {
                  if(strncmp(buffer, "START", 5) == 0){
                     demarrer_partie(board, points);
                     current_player = 1;
                     snprintf(buffer, BUF_SIZE, "Nouvelle partie démarrée !");

                     //boucle pour envoyer a tous les clients mais il faudra changer pour envoyer qu'aux deux clients connecté sur le jeu
                     for(int j = 0; j < actual; j++){
                        envoyer_etat_jeu(clients[j].sock, board, points, current_player);
                     }
      
                  }
                  else if (strncmp(buffer, "MOVE", 4) == 0){
                     //si ce n'est pas le bon jouruer qui joue, on ignore le coup
                     if (client.player != current_player) continue;

                     //si le coup est invalide, on envoie un message d'erreur au client et on ignore le coup
                     coup_valide = traiter_coup(board, points, current_player, atoi(buffer + 5));
                     if (!coup_valide){
                        snprintf(buffer, BUF_SIZE, "Coup invalide !");
                        continue;
                     }
                     
                     //boucle pour envoyer a tous les clients mais il faudra changer pour envoyer qu'aux deux clients connecté sur le jeu
                     for(int j = 0; j < actual; j++){
                        envoyer_etat_jeu(clients[j].sock, board, points, current_player);
                     }

                     current_player = current_player == 1 ? 2 : 1;
                  }
                  else if (strcmp(buffer, "SEND")== 0){
                     send_message_to_all_clients(clients, client, actual, buffer, 0);
                  }
                  else {
                     snprintf(buffer, BUF_SIZE, "Commande inconnue !");
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
   init();

   app();

   end();

   return EXIT_SUCCESS;
}
