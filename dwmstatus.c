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

Display *dpy = NULL;
int screen;
Window root;

int battery();
const char *build_status();
const char *date();
void die(const char *fmt, ...);
const char *ip_address();
int temperature();
int wireless_state();

static const int TIMEOUT = 30;

int main()
{
  const char *status;

  if (!(dpy = XOpenDisplay(getenv("DISPLAY"))))
    die("Cannot open display %s", XDisplayName(getenv("DISPLAY")));

  root = RootWindow(dpy, screen);
  screen = DefaultScreen(dpy);

  while (1) {
    status = build_status();
    XStoreName(dpy, root, status);
    XFlush(dpy);
    sleep(TIMEOUT);
  }

  XCloseDisplay(dpy);

  return 0;
}

static const char *BATTERY_NOW_FILE = "/sys/class/power_supply/BAT0/charge_now";
static const char *BATTERY_FULL_FILE = "/sys/class/power_supply/BAT0/charge_full";
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

  fclose(f);
  if (ferror(f))
    fprintf(stderr, "Error with file %s", BATTERY_NOW_FILE);

  FOPEN(f, BATTERY_FULL_FILE);
  if (f == NULL)
    return -1;


  fread(buf, sizeof *buf, 7, f);
  buf[7] = 0;
  n /= atoi(buf);

  fclose(f);
  if (ferror(f))
    fprintf(stderr, "Error with file %s", BATTERY_FULL_FILE);

  return n;
}

const char *build_status()
{
  char *res, *wireless_str, *battery_str;
  const char *d, *ip;
  int b, w;

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

  snprintf(res, 256, "%s | ⚡ %s | %d°C | %s",
           wireless_str, battery_str, temperature(), d);

  free((void *)d);

  ip_address();
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

static const char *INTERFACE = "eth0";
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

static const char *TEMPERATURE_FILE = "/sys/class/hwmon/hwmon0/temp1_input";
int temperature()
{
  FILE *f;
  char buf[3];

  FOPEN(f, TEMPERATURE_FILE);

  fread(buf, sizeof *buf, 2, f);
  buf[2] = 0;
  fclose(f);

  if (ferror(f))
    die("Error with file %s", TEMPERATURE_FILE);

  return atoi(buf);
}

static const char *WIRELESS_FILE = "/sys/class/net/eth0/carrier";
int wireless_state()
{
  FILE *f;
  char buf;

  FOPEN(f, WIRELESS_FILE);
  fread(&buf, sizeof buf, 1, f);
  fclose(f);
  if (ferror(f))
    die("Error with file %s", WIRELESS_FILE);

  return buf == '1';
}
