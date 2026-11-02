#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include "client2.h"

// void client_afficher_etat_jeu(const int board[12], const int points[2], int current_player, char* username)
// {
//    int i = 0;

//    printf("\nPlateau :\n");
//    printf("Joueur 2 : ");
//    for(i = 11; i >= 6; i--)
//    {
//       printf("%2d ", board[i]);
//    }
//    printf("\n            ");
//    for(i = 6; i >= 1; i--)
//    {
//       printf("%2d ", i);
//    }

//    printf("\nJoueur 1 : ");
//    for(i = 0; i < 6; i++)
//    {
//       printf("%2d ", board[i]);
//    }
//    printf("\n            ");
//    for(i = 1; i <= 6; i++)
//    {
//       printf("%2d ", i);
//    }

//    printf("\n\nScores : joueur 1 = %d, joueur 2 = %d\n", points[0], points[1]);
//    if(current_player == 1 || current_player == 2)
//    {
//       printf("Au tour du joueur %s.\n", username);
//    }
// }

int client_demander_coup(void)
{
   char input[BUF_SIZE];

   while(1)
   {
      char *end = NULL;
      long pit = 0;
      int line_too_long = 0;

      printf("Choisissez une case de 1 a 6 : ");
      fflush(stdout);

      if(fgets(input, sizeof(input), stdin) == NULL)
      {
         return -1;
      }

      if(strchr(input, '\n') == NULL && !feof(stdin))
      {
         int character = 0;
         line_too_long = 1;
         while((character = getchar()) != '\n' && character != EOF)
         {
         }
      }

      errno = 0;
      pit = strtol(input, &end, 10);
      while(isspace((unsigned char)*end))
      {
         end++;
      }

      if(!line_too_long && end != input && errno != ERANGE &&
         *end == '\0' && pit >= 1 && pit <= 6)
      {
         return (int)pit;
      }

      printf("Saisie invalide : entrez un nombre de 1 a 6.\n");
   }
}

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

static void app(const char *address, const char *name)
{
   SOCKET sock = init_connection(address);
   char buffer[BUF_SIZE];

   fd_set rdfs;

   /* send our name */
   write_server(sock, name);

   while(1)
   {
      FD_ZERO(&rdfs);

      /* add STDIN_FILENO */
      FD_SET(STDIN_FILENO, &rdfs);

      /* add the socket */
      FD_SET(sock, &rdfs);

      if(select(sock + 1, &rdfs, NULL, NULL, NULL) == -1)
      {
         perror("select()");
         exit(errno);
      }

      /* something from standard input : i.e keyboard */
      if(FD_ISSET(STDIN_FILENO, &rdfs))
      {
         fgets(buffer, BUF_SIZE - 1, stdin);
         {
            char *p = NULL;
            p = strstr(buffer, "\n");
            if(p != NULL)
            {
               *p = 0;
            }
            else
            {
               /* fclean */
               buffer[BUF_SIZE - 1] = 0;
            }
         }
         write_server(sock, buffer);
      }
      else if(FD_ISSET(sock, &rdfs))
      {
         int n = read_server(sock, buffer);
         /* server down */
         if(n == 0)
         {
            printf("Server disconnected !\n");
            break;
         }
         puts(buffer);
      }
   }

   end_connection(sock);
}

static int init_connection(const char *address)
{
   SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
   SOCKADDR_IN sin = { 0 };
   struct hostent *hostinfo;

   if(sock == INVALID_SOCKET)
   {
      perror("socket()");
      exit(errno);
   }

   hostinfo = gethostbyname(address);
   if (hostinfo == NULL)
   {
      fprintf (stderr, "Unknown host %s.\n", address);
      exit(EXIT_FAILURE);
   }

   sin.sin_addr = *(IN_ADDR *) hostinfo->h_addr;
   sin.sin_port = htons(PORT);
   sin.sin_family = AF_INET;

   if(connect(sock,(SOCKADDR *) &sin, sizeof(SOCKADDR)) == SOCKET_ERROR)
   {
      perror("connect()");
      exit(errno);
   }

   return sock;
}

static void end_connection(int sock)
{
   closesocket(sock);
}

static int read_server(SOCKET sock, char *buffer)
{
   int n = 0;

   if((n = recv(sock, buffer, BUF_SIZE - 1, 0)) < 0)
   {
      perror("recv()");
      exit(errno);
   }

   buffer[n] = 0;

   return n;
}

static void write_server(SOCKET sock, const char *buffer)
{
   if(send(sock, buffer, strlen(buffer), 0) < 0)
   {
      perror("send()");
      exit(errno);
   }
}

int main(int argc, char **argv)
{

   if(argc < 3)
   {
      printf("Usage : %s [address] [pseudo]\n", argv[0]);
      return EXIT_FAILURE;
   }

   init();

   app(argv[1], argv[2]);

   end();

   return EXIT_SUCCESS;
}
