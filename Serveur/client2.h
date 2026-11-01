#ifndef CLIENT_H
#define CLIENT_H

#include "server2.h"

typedef struct
{
   SOCKET sock;
   char name[BUF_SIZE];
   int player; /* 1 ou 2*/
}Client;

#endif /* guard */
