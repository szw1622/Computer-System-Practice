/*
 * friendlist.c - [Starting code for] a web-based friend-graph manager.
 *
 * Based on:
 *  tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *      GET method to serve static and dynamic content.
 *   Tiny Web server
 *   Dave O'Hallaron
 *   Carnegie Mellon University
 */
#include "csapp.h"
#include "dictionary.h"
#include "more_string.h"

static void doit(int fd);
static dictionary_t *read_requesthdrs(rio_t *rp);
static void read_postquery(rio_t *rp, dictionary_t *headers, dictionary_t *d);
static void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                        char *longmsg);
static void print_stringdictionary(dictionary_t *d);

// responses
static void serve_friends(int fd, dictionary_t *query);
static void serve_introduce(int fd, dictionary_t *query);
static void serve_befriend(int fd, dictionary_t *query);
static void serve_unfriend(int fd, dictionary_t *query);
// static void serve_greet(int fd, dictionary_t *query);

// helper functions
static void showPage(int fd, char *body);
void *Athread(void *con);
// varibles
dictionary_t *friends;
pthread_mutex_t lock;

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  pthread_mutex_init(&lock, NULL);
  listenfd = Open_listenfd(argv[1]);
  friends = make_dictionary(COMPARE_CASE_SENS, free);

  /* Don't kill the server if there's an error, because
     we want to survive errors due to a client. But we
     do want to report errors. */
  exit_on_error(0);

  /* Also, don't stop on broken connections: */
  Signal(SIGPIPE, SIG_IGN);

  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    if (connfd >= 0) {
      Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port,
                  MAXLINE, 0);
      printf("Accepted connection from (%s, %s)\n", hostname, port);

      // Request memory
      int *con = malloc(sizeof(int));
      *con = connfd;
      // Create a new thread to run the doit function
      pthread_t thread;
      Pthread_create(&thread, NULL, Athread, con);
      Pthread_detach(thread);
    }
  }
}

/**
 * Create a new thread to run the doit function
 */
void *Athread(void *con) {
  int c = *(int *)con;
  free(con);
  doit(c);
  Close(c);
  return NULL;
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd) {
  char buf[MAXLINE], *method, *uri, *version;
  rio_t rio;
  dictionary_t *headers, *query;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  if (Rio_readlineb(&rio, buf, MAXLINE) <= 0)
    return;
  printf("%s", buf);

  if (!parse_request_line(buf, &method, &uri, &version)) {
    clienterror(fd, method, "400", "Bad Request",
                "Friendlist did not recognize the request");
  } else {
    if (strcasecmp(version, "HTTP/1.0") && strcasecmp(version, "HTTP/1.1")) {
      clienterror(fd, version, "501", "Not Implemented",
                  "Friendlist does not implement that version");
    } else if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
      clienterror(fd, method, "501", "Not Implemented",
                  "Friendlist does not implement that method");
    } else {
      headers = read_requesthdrs(&rio);

      /* Parse all query arguments into a dictionary */
      query = make_dictionary(COMPARE_CASE_SENS, free);

      parse_uriquery(uri, query);
      if (!strcasecmp(method, "POST"))
        read_postquery(&rio, headers, query);

      /* For debugging, print the dictionary */
      print_stringdictionary(query);

      /* You'll want to handle different queries here,
         but the intial implementation always returns
         nothing: */
      if (starts_with("/friends", uri)) {
        pthread_mutex_lock(&lock);
        serve_friends(fd, query);
        pthread_mutex_unlock(&lock);
      } else if (starts_with("/befriend", uri)) {
        pthread_mutex_lock(&lock);
        serve_befriend(fd, query);
        pthread_mutex_unlock(&lock);
      } else if (starts_with("/unfriend", uri)) {
        pthread_mutex_lock(&lock);
        serve_unfriend(fd, query);
        pthread_mutex_unlock(&lock);
      } else if (starts_with("/introduce", uri)) {
        serve_introduce(fd, query);
      }
      // else
      //   serve_request(fd, query);

      /* Clean up */
      free_dictionary(query);
      free_dictionary(headers);
    }

    /* Clean up status line */
    free(method);
    free(uri);
    free(version);
  }
}

/*
 * read_requesthdrs - read HTTP request headers
 */
dictionary_t *read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];
  dictionary_t *d = make_dictionary(COMPARE_CASE_INSENS, free);

  Rio_readlineb(rp, buf, MAXLINE);
  printf("%s", buf);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    parse_header_line(buf, d);
  }

  return d;
}

void read_postquery(rio_t *rp, dictionary_t *headers, dictionary_t *dest) {
  char *len_str, *type, *buffer;
  int len;

  len_str = dictionary_get(headers, "Content-Length");
  len = (len_str ? atoi(len_str) : 0);

  type = dictionary_get(headers, "Content-Type");

  buffer = malloc(len + 1);
  Rio_readnb(rp, buffer, len);
  buffer[len] = 0;

  if (!strcasecmp(type, "application/x-www-form-urlencoded")) {
    parse_query(buffer, dest);
  }

  free(buffer);
}

static char *ok_header(size_t len, const char *content_type) {
  char *len_str, *header;

  header = append_strings(
      "HTTP/1.0 200 OK\r\n", "Server: Friendlist Web Server\r\n",
      "Connection: close\r\n", "Content-length: ", len_str = to_string(len),
      "\r\n", "Content-type: ", content_type, "\r\n\r\n", NULL);

  free(len_str);

  return header;
}

/**
 * Helper method to show the friedns on the page
 */
static void showPage(int fd, char *body) {
  size_t len = strlen(body);
  char *header;

  /* Send response headers to client */
  header = ok_header(len, "text/html; charset=utf-8");
  Rio_writen(fd, header, strlen(header));
  printf("Response headers:\n");
  printf("%s", header);

  free(header);

  /* Send response body to client */
  Rio_writen(fd, body, len);
}

// All of the friends of the user
static void serve_friends(int fd, dictionary_t *query) {
  char *body;
  char *user = dictionary_get(query, "user");
  dictionary_t *userFriends = dictionary_get(friends, user);

  if (!userFriends) {
    body = "";
  } else {
    body = join_strings(dictionary_keys(userFriends), '\n');
  }

  showPage(fd, body);
}

// add friend
static void serve_befriend(int fd, dictionary_t *query) {
  char *body;
  const char *user = dictionary_get(query, "user");
  // get new friend list
  char **newFriends = split_string(dictionary_get(query, "friends"), '\n');

  int i = 0;
  // scanning
  while (newFriends[i]) {
    // If scanning node is not equal to the user
    if (strcmp(newFriends[i], user)) {
      // add friending to user
      if (!dictionary_get(friends, newFriends[i])) {
        dictionary_set(friends, newFriends[i],
                       make_dictionary(COMPARE_CASE_INSENS, free));
      }
      dictionary_set(dictionary_get(friends, newFriends[i]), user, NULL);
      // add user to friending
      if (!dictionary_get(friends, user)) {
        dictionary_set(friends, user,
                       make_dictionary(COMPARE_CASE_INSENS, free));
      }
      dictionary_set(dictionary_get(friends, user), newFriends[i], NULL);
    }
    i++;
  }

  body = join_strings(dictionary_keys(dictionary_get(friends, user)), '\n');
  showPage(fd, body);
}

// remove friend
static void serve_unfriend(int fd, dictionary_t *query) {
  char *body;
  const char *user = dictionary_get(query, "user");
  dictionary_t *user_friends = dictionary_get(friends, user);
  // get unfriend list
  char **unfriends = split_string(dictionary_get(query, "friends"), '\n');

  int i = 0;
  // scanning the unfriending list
  while (unfriends[i]) {
    char *unfriending = unfriends[i]; // scanning node
    // remove unfriending
    dictionary_remove(user_friends, unfriending);
    // remove user
    dictionary_remove(dictionary_get(friends, unfriending), user);

    i++;
  }

  body = join_strings(dictionary_keys(dictionary_get(friends, user)), '\n');
  showPage(fd, body);
}

// add all as the user's friends
static void serve_introduce(int fd, dictionary_t *query) {
  char *body;
  char *host = dictionary_get(query, "host");
  char *port = dictionary_get(query, "port");
  const char *friend = dictionary_get(query, "friend");
  const char *user = dictionary_get(query, "user");

  // create buffer
  char buf[MAXBUF];
  int client = Open_clientfd(host, port);
  sprintf(buf, "GET /friends?user=%s HTTP/1.1\r\n\r\n", query_encode(friend));
  Rio_writen(client, buf, strlen(buf));
  Shutdown(client, SHUT_WR);
  rio_t rio;
  Rio_readinitb(&rio, client);

  dictionary_t *headers = read_requesthdrs(&rio);
  char *len_str = dictionary_get(headers, "Content-length");
  int len = (len_str ? atoi(len_str) : 0);
  char rec_buf[len];
  Rio_readnb(&rio, rec_buf, len);
  rec_buf[len] = 0;

  // thread start
  pthread_mutex_lock(&lock);
  char **newFriends = split_string(rec_buf, '\n');
  int i = 0;
  // scanning
  while (newFriends[i]) {
    // If scanning node is not equal to the user
    if (strcmp(newFriends[i], user)) {
      // add friending to user
      if (!dictionary_get(friends, newFriends[i])) {
        dictionary_set(friends, newFriends[i],
                       make_dictionary(COMPARE_CASE_INSENS, free));
      }
      dictionary_set(dictionary_get(friends, newFriends[i]), user, NULL);
      // add user to friending
      if (!dictionary_get(friends, user)) {
        dictionary_set(friends, user,
                       make_dictionary(COMPARE_CASE_INSENS, free));
      }
      dictionary_set(dictionary_get(friends, user), newFriends[i], NULL);
    }
    i++;
  }
  free(newFriends);
  pthread_mutex_unlock(&lock);
  // thread end

  body = join_strings(dictionary_keys(dictionary_get(friends, user)), '\n');
  showPage(fd, body);

  free(body);
  Close(client);
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
  size_t len;
  char *header, *body, *len_str;

  body = append_strings("<html><title>Friendlist Error</title>",
                        "<body bgcolor="
                        "ffffff"
                        ">\r\n",
                        errnum, " ", shortmsg, "<p>", longmsg, ": ", cause,
                        "<hr><em>Friendlist Server</em>\r\n", NULL);
  len = strlen(body);

  /* Print the HTTP response */
  header = append_strings("HTTP/1.0 ", errnum, " ", shortmsg, "\r\n",
                          "Content-type: text/html; charset=utf-8\r\n",
                          "Content-length: ", len_str = to_string(len),
                          "\r\n\r\n", NULL);
  free(len_str);

  Rio_writen(fd, header, strlen(header));
  Rio_writen(fd, body, len);

  free(header);
  free(body);
}

static void print_stringdictionary(dictionary_t *d) {
  int i, count;

  count = dictionary_count(d);
  for (i = 0; i < count; i++) {
    printf("%s=%s\n", dictionary_key(d, i),
           (const char *)dictionary_value(d, i));
  }
  printf("\n");
}