CC = gcc
CFLAGS = -Wall -Wextra

SERVER = Serveur/server2
CLIENT = Client/client2

.PHONY: all clean

all: $(SERVER) $(CLIENT)

$(SERVER): Serveur/server2.c Serveur/awale.c Serveur/server2.h Serveur/client2.h Serveur/awale.h
	$(CC) $(CFLAGS) Serveur/server2.c Serveur/awale.c -o $(SERVER)

$(CLIENT): Client/client2.c Client/client2.h
	$(CC) $(CFLAGS) Client/client2.c -o $(CLIENT)

clean:
	rm -f $(SERVER) $(CLIENT)
