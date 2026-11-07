#ifndef CLIENT_H
#define CLIENT_H

#include "server2.h"
#include <stdbool.h>

typedef struct
{
   SOCKET sock;
   char name[BUF_SIZE];
   bool available;
   char invited_by[BUF_SIZE];
   char bio[BUF_SIZE];
}Client;

#endif /* guard */
