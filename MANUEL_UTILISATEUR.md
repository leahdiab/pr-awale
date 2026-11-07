# Manuel utilisateur - Awale en reseau

## 1. Prerequis

- Un terminal sur Linux ou un environnement compatible avec les sockets utilises par le projet.
- `gcc` et `make`.
- Un acces reseau entre les ordinateurs qui lancent le serveur et les clients.

Le serveur ecoute sur le port TCP `1977`. Ce port doit etre accessible depuis les machines clientes.

## 2. Compilation

Depuis la racine du depot :

```sh
make
```

Cette commande compile :

- le serveur dans `Serveur/server2` ;
- le client dans `Client/client2`.

Pour compiler et executer les tests des regles de jeu :

```sh
make test
```

Ces tests portent sur les regles de l'Awale ; ils ne lancent pas un test complet du reseau.

Pour supprimer les executables compiles :

```sh
make clean
```

## 3. Lancer le serveur et se connecter

### Demarrer le serveur

Dans un premier terminal, a la racine du depot :

```sh
./Serveur/server2
```

Laissez ce terminal ouvert pendant les parties. Pour arreter le serveur normalement, appuyez sur Entree dans son terminal.

### Connecter un joueur

Dans un autre terminal :

```sh
./Client/client2 127.0.0.1 Alice
```

Remplacez `Alice` par le pseudo souhaite. Pour un serveur sur une autre machine, remplacez `127.0.0.1` par son nom d'hote ou son adresse IP, par exemple :

```sh
./Client/client2 192.168.1.20 Alice
```

Lancez une commande de client par joueur, dans un terminal distinct. Les pseudos doivent etre uniques parmi les joueurs connectes.

## 4. Aide et saisie des commandes

Apres la connexion, le serveur envoie la liste des commandes disponibles. Vous pouvez la revoir avec :

```text
HELP
```

Saisissez une commande puis appuyez sur Entree. Les commandes sont a envoyer dans le terminal du client, pas dans celui du serveur.

## 5. Jouer une partie

### Defier un joueur

1. Affichez les joueurs connectes :

   ```text
   LIST PLAYERS
   ```

2. Envoyez un defi :

   ```text
   CHALLENGE Bob
   ```

3. Le joueur invite repond :

   ```text
   ACCEPT
   ```

   ou refuse avec :

   ```text
   REFUSE
   ```

4. Apres l'acceptation, le createur choisit la visibilite de la partie :

   ```text
   PUBLIC
   ```

   ou :

   ```text
   PRIVE
   ```

   Le choix doit etre fait par le createur avant de jouer.

### Jouer un coup

Quand c'est votre tour, choisissez une case de votre camp, numerotee de 1 a 6 :

```text
MOVE 3
```

La partie suit les regles de l'Awale. Un coup doit notamment nourrir le camp adverse si celui-ci est vide. Une capture qui viderait entierement le camp adverse n'est pas effectuee.

La partie se termine lorsqu'un joueur atteint 25 points ou lorsqu'aucun coup ne permet de continuer. Le serveur affiche alors le plateau final, les scores et le resultat, puis demande si la partie doit etre sauvegardee.

Pour terminer volontairement la partie :

```text
END
```

Les joueurs sont ensuite invites a repondre `OUI` ou `NON` pour la sauvegarde. La partie est conservee si un joueur repond `OUI`.

## 6. Amis

### Envoyer et traiter une demande

Envoyez une demande :

```text
ADD FRIEND Bob
```

Le destinataire peut l'accepter ou la refuser en precisant le pseudo de l'expediteur :

```text
ACCEPT FRIEND Alice
```

```text
REFUSE FRIEND Alice
```

Une amitie acceptee est reciproque. `LIST FRIENDS` affiche les amis et les demandes recues :

```text
LIST FRIENDS
```

Pour retirer une personne de sa liste d'amis :

```text
REMOVE FRIEND Bob
```

L'amitie est supprimee des deux cotes. Les amities acceptees sont enregistrees dans `friends.txt`, dans le repertoire de travail depuis lequel le serveur a ete lance. Les demandes en attente ne sont pas conservees apres l'arret du serveur.

## 7. Spectateurs et parties privees

Afficher les parties en cours :

```text
LIST GAMES
```

Regarder une partie a l'aide de son numero :

```text
WATCH 1
```

Toute personne peut regarder une partie publique. Pour une partie privee, il faut etre ami avec au moins un des deux joueurs. Sinon, le serveur refuse l'acces et indique la commande `ADD FRIEND pseudo`.

Arreter le visionnage :

```text
STOP WATCH
```

Le spectateur cesse alors de recevoir les mises a jour de la partie.

## 8. Messages et biographies

Envoyer un message a tous les autres joueurs connectes :

```text
SEND PUBLIC Bonjour tout le monde !
```

Envoyer un message prive :

```text
SEND PRIVATE Bob Salut !
```

Ecrire ou modifier sa biographie :

```text
WRITE BIO J'aime jouer a l'Awale.
```

Lire celle d'un joueur :

```text
READ BIO Bob
```

## 9. Revoir une partie sauvegardee

Demander la liste des parties sauvegardees :

```text
REVIEW
```

Entrez ensuite le numero affiche dans la liste. Pour avancer dans la relecture :

```text
NEXT
```

A la fin, utilisez de nouveau `REVIEW` pour choisir une autre partie. Les sauvegardes sont stockees dans `saved_games.txt`, dans le repertoire de travail du serveur.

## 10. Fichiers de donnees et depannage

- `friends.txt` contient les liens d'amitie persistants.
- `saved_games.txt` contient les parties sauvegardees.
- Ces fichiers sont lus et ecrits dans le repertoire courant du processus serveur. Lancez le serveur depuis un repertoire stable pour retrouver les memes donnees a chaque demarrage.
- Si un client ne parvient pas a se connecter, verifiez que le serveur fonctionne, que l'adresse IP est correcte et que le port TCP `1977` n'est pas bloque par un pare-feu.
- Si une commande est refusee, utilisez `HELP` et verifiez son orthographe ainsi que les arguments demandes.
