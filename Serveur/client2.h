#ifndef CLIENT_H
#define CLIENT_H

#include "server2.h"
#include <stdbool.h>
#include <stddef.h>

/* Etat d'un joueur connecte, conserve par le serveur. */
typedef struct
{
   SOCKET sock;
   char name[BUF_SIZE];
   bool available;
   char invited_by[BUF_SIZE];
   char bio[BUF_SIZE];
   bool review_selection_pending;
   bool reviewing;
   size_t review_game_index;
   size_t review_game_count;
   size_t review_state_index;
}Client;

#endif
