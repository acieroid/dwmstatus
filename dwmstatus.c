#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>

/* TODO:
 * Add OSS volume ?
 */

#define ALLOCATE(var, n) \
  if (!((var) = calloc((n), sizeof *(var)))) { \
    if (dpy && root) \
      XStoreName(dpy, root, "Memory error"); \
    die("Cannot allocate %u bytes", n * sizeof *(var)); \
  }

#define FOPEN(f, file) \
  if (!((f) = fopen((file), "r"))) { \
    fprintf(stderr, "Cannot open file %s\n", (file)); \
  }

#define INTERFACE "wlan0"
static const int TIMEOUT = 30;
static const char *BATTERY_NOW_FILE = "/sys/class/power_supply/BAT0/charge_now";
static const char *BATTERY_FULL_FILE = "/sys/class/power_supply/BAT0/charge_full";
static const char *TEMPERATURE_FILE = "/sys/class/hwmon/hwmon0/temp1_input";
static const char *WIRELESS_FILE = "/sys/class/net/" INTERFACE "/carrier";
static const int MPD_PORT = 6600;
static const char *MPD_SERVER = "127.0.0.1";

typedef struct {
  char artist[256];
  char title[256];
  char album[256];
  char filename[256];
} mpd_info_t;

Display *dpy = NULL;
int screen;
Window root;

int battery();
const char *build_status();
const char *date();
void die(const char *fmt, ...);
const char *ip_address();
mpd_info_t *mpd();
void sigint_handler(int signum);
int temperature();
int wireless_state();

int main()
{
  const char *status;

  signal(SIGINT, sigint_handler);

  if (!(dpy = XOpenDisplay(getenv("DISPLAY"))))
    die("Cannot open display %s", XDisplayName(getenv("DISPLAY")));

  root = RootWindow(dpy, screen);
  screen = DefaultScreen(dpy);

  while (1) {
    status = build_status();
    XStoreName(dpy, root, status);
    free((void *)status);
    XFlush(dpy);
    sleep(TIMEOUT);
  }

  XCloseDisplay(dpy);

  return 0;
}

int battery()
{
  FILE *f;
  char buf[8];
  int n;

  FOPEN(f, BATTERY_NOW_FILE);
  if (f == NULL)
    return -1;

  fread(buf, sizeof *buf, 7, f);
  buf[7] = 0;
  n = 100*atoi(buf);

  if (fclose(f) == EOF)
    perror("fclose()");

  FOPEN(f, BATTERY_FULL_FILE);
  if (f == NULL)
    return -1;

  fread(buf, sizeof *buf, 7, f);
  buf[7] = 0;
  n /= atoi(buf);

  if (fclose(f) == EOF)
    perror("fclose()");

  return n;
}

const char *build_status()
{
  char *res, *wireless_str, *battery_str, *mpd_str;
  const char *d, *ip;
  int b, w;
  mpd_info_t *info;

  ALLOCATE(res, 256);

  d = date();

  b = battery();
  ALLOCATE(battery_str, 5);
  if (b == -1)
    strncpy(battery_str, "no", 2);
  else
    snprintf(battery_str, 4, "%d%%", b);

  w = wireless_state();
  ALLOCATE(wireless_str, 5 + INET_ADDRSTRLEN);
  if (w) {
    ip = ip_address();
    snprintf(wireless_str, 4 + INET_ADDRSTRLEN, "✓ %s", ip);
    free((void *)ip);
  }
  else {
    strncpy(wireless_str, "✗", 3);
  }

  info = mpd();
  ALLOCATE(mpd_str, 255);
  if (info)
    if (strlen(info->artist) > 0 &&
        strlen(info->title) > 0)
      snprintf(mpd_str, strlen(info->artist) + strlen(info->title) + 4,
               "%s - %s", info->artist, info->title);
    else
      strncpy(mpd_str, info->filename, strlen(info->filename)+1);
  else
    strncpy(mpd_str, "no", 3);
  free(info);

  snprintf(res, 256, "♫ %s | %s | ⚡ %s | %d°C | %s",
           mpd_str, wireless_str, battery_str, temperature(), d);

  free(battery_str);
  free(wireless_str);
  free(mpd_str);
  free((void *)d);

  return res;
}

const char *date()
{
  time_t t;
  char *res;

  ALLOCATE(res, 21);

  t = time(NULL);
  strftime(res, 20, "%D %H:%M", localtime(&t));
  return res;
}

void die(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  exit(1);
}

const char *ip_address()
{
  struct ifaddrs *ifaddr, *p;
  char *ip;
  void *tmp;

  ALLOCATE(ip, INET_ADDRSTRLEN);
  getifaddrs(&ifaddr);

  for (p = ifaddr; p != NULL; p = p->ifa_next) {
    if (strcmp(p->ifa_name, INTERFACE) == 0 &&
        p->ifa_addr->sa_family == AF_INET) {
      /* We don't care about IPV6 addresses for the moment */
      tmp = &((struct sockaddr_in *)p->ifa_addr)->sin_addr;
      inet_ntop(AF_INET, tmp, ip, INET_ADDRSTRLEN);
    }
  }

  freeifaddrs(ifaddr);
  return ip;
}

mpd_info_t *mpd()
{
  int done, n, newline_pos, colon_pos;
  int fd;
  struct sockaddr_in addr;
  char buf[256];
  mpd_info_t *info;

  ALLOCATE(info, 1);

  done = 0;
  fd = 0;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(MPD_PORT);
  addr.sin_addr.s_addr = inet_addr(MPD_SERVER);
  memset(&(addr.sin_zero), 0, sizeof addr.sin_zero);

  /* Connect to MPD */
  if ((fd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket()");
    goto fail;
  }

  if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == -1) {
    perror("connect()");
    goto fail;
  }

  /* Send the command */
  if (send(fd, "currentsong\n", strlen("currentsong\n"), 0) == -1) {
    perror("send()");
    goto fail;
  }

  /* Read the result */
  while (!done) {
    if ((n = recv(fd, &buf, sizeof buf, MSG_PEEK)) == -1) {
      perror("recv()");
      goto fail;
    }
    for (newline_pos = 0;
         newline_pos < n && buf[newline_pos] != '\n';
         newline_pos++)
      ;

    if (newline_pos == 0)
      continue;

    if ((n = recv(fd, &buf, newline_pos+1, MSG_WAITALL)) == -1) {
      perror("recv()");
      goto fail;
    }
    buf[newline_pos] = '\0';

    if (strcmp(buf, "OK") == 0)
      done = 1;

    for (colon_pos = 0;
         colon_pos < n && buf[colon_pos] != ':';
         colon_pos++)
      ;
    if (strncmp(buf, "Artist", colon_pos) == 0)
      strncpy(info->artist, buf+colon_pos+2, newline_pos - colon_pos - 2);
    else if (strncmp(buf, "Title", colon_pos) == 0)
      strncpy(info->title, buf+colon_pos+2, newline_pos - colon_pos - 2);
    else if (strncmp(buf, "Album", colon_pos) == 0)
      strncpy(info->album, buf+colon_pos+2, newline_pos - colon_pos - 2);
    else if (strncmp(buf, "filename", colon_pos) == 0)
      strncpy(info->filename, buf+colon_pos+2, newline_pos - colon_pos - 2);
  }

  /* Close the connection */
  if (close(fd) == -1)
    perror("close()");

  return info;

fail:
  free(info);
  return NULL;
}

void sigint_handler(int signum)
{
  XCloseDisplay(dpy);
  exit(0);
}

int temperature()
{
  FILE *f;
  char buf[3];

  FOPEN(f, TEMPERATURE_FILE);
  if (f == NULL)
    return 0;

  fread(buf, sizeof *buf, 2, f);
  buf[2] = 0;
  if (fclose(f) == EOF)
    perror("fclose()");

  return atoi(buf);
}

int wireless_state()
{
  FILE *f;
  char buf;

  FOPEN(f, WIRELESS_FILE);
  if (f == NULL)
    return 0;

  fread(&buf, sizeof buf, 1, f);
  if (fclose(f) == EOF)
    perror("fclose()");

  return buf == '1';
}
