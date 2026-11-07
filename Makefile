CC = gcc
CFLAGS = -Wall -Wextra

SERVER = Serveur/server2
CLIENT = Client/client2
TEST = tests/test_awale

.PHONY: all clean test

all: $(SERVER) $(CLIENT)

$(SERVER): Serveur/server2.c Serveur/awale.c Serveur/server2.h Serveur/client2.h Serveur/awale.h
	$(CC) $(CFLAGS) Serveur/server2.c Serveur/awale.c -o $(SERVER)

$(CLIENT): Client/client2.c Client/client2.h
	$(CC) $(CFLAGS) Client/client2.c -o $(CLIENT)

$(TEST): tests/test_awale.c Serveur/awale.c Serveur/awale.h
	$(CC) $(CFLAGS) tests/test_awale.c Serveur/awale.c -o $(TEST)

test: $(TEST)
	./$(TEST)

clean:
	rm -f $(SERVER) $(CLIENT) $(TEST)
