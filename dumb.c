/*
    dumb - A dumb terminal with XMODEM support.
    Copyright © 2003 - 2020 Brian Stafford <brian.stafford60+dumb@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <termios.h>

#ifdef NOREADLINE
static char *
readline (const char *prompt)
{
  char buf[512];
  char *nl;

  fputs (prompt, stdout);
  if (fgets (buf, sizeof buf, stdin) == NULL)
    {
      fprintf (stderr, "can't read line\n");
      return NULL;
    }
  if ((nl = strchr (buf, '\n')) != NULL)
    *nl = '\0';
  return strdup (buf);
}

#define using_history()
#define add_history(line)
#define read_history(file)
#define write_history(file)

#else
#include <readline/readline.h>
#include <readline/history.h>
#endif

void process_command (int lfd);
void xmodem_send (int fd);

struct bauds
  {
    int baud;
    int flag;
  };
struct bauds bauds[] =
  {
    { 50, B50, },
    { 75, B75, },
    { 110, B110, },
    { 134, B134, },
    { 150, B150, },
    { 200, B200, },
    { 300, B300, },
    { 600, B600, },
    { 1200, B1200, },
    { 1800, B1800, },
    { 2400, B2400, },
    { 4800, B4800, },
    { 9600, B9600, },
    { 19200, B19200, },
    { 38400, B38400, },
    { 57600, B57600, },
    { 115200, B115200, },
    { 230400, B230400, },
  };

int
main (int argc, char **argv)
{
  int c, err, done, state;
  struct termios line_tio, cons_tio, save_tio;
  struct pollfd pollfds[2];
  const char *line, *history;
  int lfd;
  long flags;
  int baud;
  size_t i;
  int esc = 'A' & 0x1f;
  int timestamp = 0;

  line = "/dev/ttyS0";
  baud = 115200;
  history = ".dumb";
  while ((c = getopt (argc, argv, "th:l:b:e:")) != EOF)
    switch (c)
      {
      case 't': timestamp = 1; break;
      case 'h': history = optarg; break;
      case 'l': line = optarg; break;
      case 'b': baud = strtol (optarg, NULL, 10); break;
      case 'e':
      	if (*optarg == '^')
	  esc = optarg[1] & 0x1f;
	else
	  esc = strtol (optarg, NULL, 0);
	break;
      }

  using_history ();
  read_history (history);

  lfd = open (line, O_RDWR | O_EXCL | O_NDELAY);
  if (lfd < 0)
    {
      fprintf (stderr, "can't open %s: %s\n", line, strerror (errno));
      exit (1);
    }

  /* Set up the line to be raw, 115200 baud, 8bit, no parity */
  tcgetattr (lfd, &line_tio);
  cfmakeraw (&line_tio);
  line_tio.c_cflag |= CREAD;
  line_tio.c_cflag &= ~CRTSCTS;
  line_tio.c_cc[VMIN] = 1;
  line_tio.c_cc[VTIME] = 1;
  for (i = 0; i < sizeof bauds / sizeof bauds[0]; i++)
    if (bauds[i].baud >= baud)
      {
	cfsetospeed (&line_tio, bauds[i].flag);
	cfsetispeed (&line_tio, bauds[i].flag);
	break;
      }
  tcsetattr (lfd, TCSANOW, &line_tio);

  flags = fcntl (lfd, F_GETFL);
  flags &= ~O_NDELAY;
  fcntl (lfd, F_SETFL, flags);

  pollfds[0].fd = 0;
  pollfds[0].events = POLLIN;
  pollfds[1].fd = lfd;
  pollfds[1].events = POLLIN;

  /* Save the console and set it to be raw */
  tcgetattr (1, &save_tio);
  cons_tio = save_tio;
  cfmakeraw (&cons_tio);
  cons_tio.c_cc[VMIN] = 1;
  cons_tio.c_cc[VTIME] = 0;
  tcsetattr (1, TCSANOW, &cons_tio);

  err = 0;
  state = 0;
  done = 0;
  while (!done)
    {
      if (poll (pollfds, sizeof pollfds / sizeof pollfds[0], -1) < 0)
	{
	  if (errno == EINTR)
	    continue;
	  err = errno;
	  break;
	}
      if (pollfds[0].revents & POLLIN)
        {
          char ch[40];
	  int nch;

          if ((nch = read (0, ch, sizeof ch)) <= 0)
            continue;
	  switch (state)
	    {
	    case 1:
	      if (ch[0] == esc)
		write (lfd, ch, nch);
	      else if (ch[0] == 't' || ch[0] == 'T')
	        {
		  timestamp = !timestamp;
		}
	      else if (ch[0] == 'c' || ch[0] == 'C')
	        {
		  tcsetattr (1, TCSANOW, &save_tio);
		  putchar ('\n');
		  process_command (lfd);
		  tcsetattr (1, TCSANOW, &cons_tio);
	        }
	      else if (ch[0] == 's' || ch[0] == 'S')
	        {
		  tcsetattr (1, TCSANOW, &save_tio);
		  putchar ('\n');
		  xmodem_send (lfd);
		  tcsetattr (1, TCSANOW, &cons_tio);
	        }
	      else if (ch[0] == 'h' || ch[0] == 'H')
	        {
		  char *line;

		  tcsetattr (1, TCSANOW, &save_tio);
		  putchar ('\n');
		  line = readline ("! ");
		  tcsetattr (1, TCSANOW, &cons_tio);
		  if (line != NULL)
		    {
		      if (*line != '\0')
			{
			  add_history (line);
			  write (lfd, line, strlen (line));
			}
		      free (line);
		    }
	        }
	      else if (ch[0] == 'x' || ch[0] == 'X')
	        done = 1;
	      state = 0;
	      break;

	    default:
	      state = 0;
	      if (nch == 1 && ch[0] == esc)
	        state = 1;
	      else
		write (lfd, ch, nch);
	      break;
	    }
        }
      if (pollfds[1].revents & POLLIN)
        {
          char buf[256];
          int n;

          n = read (lfd, buf, sizeof buf);
	  if (timestamp)
	    {
	      struct timespec ts;
	      struct tm tm;
	      char tmbuf[40];
	      double seconds;

	      if (clock_gettime (CLOCK_REALTIME, &ts) == 0)
	        {
		  strftime (tmbuf, sizeof tmbuf, "%H:%M", gmtime_r (&ts.tv_sec, &tm));
		  seconds = (double) tm.tm_sec + ts.tv_nsec / 1e9;
		  printf ("[%s %f]", tmbuf, seconds);
		  fflush (stdout);
		}
	    }
          write (1, buf, n);
        }
    }

  /* Restore console to original settings */
  tcsetattr (1, TCSANOW, &save_tio);
  if (err != 0)
    fprintf (stderr, "poll %s: %s\n", line, strerror (err));

  write_history (history);
  exit (0);
}

void
process_command (int lfd)
{
  char *cmd;
  pid_t pid;
  int status;

  if ((cmd = readline ("> ")) == NULL)
    return;
  if (*cmd == '\0')
    {
      free (cmd);
      return;
    }
  add_history (cmd);

  if ((pid = fork ()) < 0)
    {
      fprintf (stderr, "cannot fork %s", strerror (errno));
      free (cmd);
      return;
    }

  if (pid == 0)
    {
      dup2 (lfd, 0);
      dup2 (lfd, 1);
      execl ("/bin/sh", "sh", "-c", cmd, NULL);
    }

  waitpid (pid, &status, 0);
  fprintf (stderr, "Done %s\n", cmd);
  free (cmd);
}


/* Simple XModem send routine */

#define SOH  0x01
#define STX  0x02
#define EOT  0x04
#define ACK  0x06
#define NAK  0x15
#define CAN  0x18

unsigned short crc16_ccitt (const void *buf, int len);

static int
send_packet (int fd, char buf[], int len, int packet_size, int sequence)
{
  unsigned char ch, hdr[3], check[2];
  int ccrc, retry;

  if (len > 0)
    {
      if (len < packet_size)
	memset (buf + len, 'Z' & 0x1f, packet_size - len);
      ccrc = crc16_ccitt (buf, packet_size);
      hdr[0] = (packet_size == 1024) ? STX : SOH;
      hdr[1] = sequence & 0xFF;
      hdr[2] = ~(sequence & 0xFF);
      check[0] = ccrc >> 8;
      check[1] = ccrc;
    }

  /* retry packet at most 10 times */
  for (retry = 0; retry < 10; retry++)
    {
      fflush (stderr);
      if (len > 0)
	{
	  write (fd, hdr, sizeof hdr);
	  write (fd, buf, packet_size);
	  write (fd, check, sizeof check);
	}
      else
	{
	  ch = EOT;
	  write (fd, &ch, 1);
	}

      /* Wait for ACK */
      if (read (fd, &ch, 1) != 1)
        ch = 0;
      if (ch == ACK)
	return 0;
      else if (ch != NAK)
        {
	  fprintf (stderr, "abort - lost sync\n");
	  return -1;
        }
    }
  fprintf (stderr, "abort - too many failures\n");
  return -2;
}

static const char spinner[] = "|/-\\";

void
xmodem_send (int fd)
{
  FILE *fp;
  int retry, len, sequence = 0, packet_size = 128;
  struct termios save_tio, line_tio;
  char *filename, buf[1024];
  unsigned char ch;
  struct stat st;
  long pct, sent;

  if ((filename = readline ("send file> ")) == NULL)
    return;
  if (*filename == '\0')
    {
      free (filename);
      return;
    }
  add_history (filename);

  fp = fopen (filename, "rb");
  if (fp == NULL)
    {
      fprintf (stderr, "can't open %s: %s\n", filename, strerror (errno));
      free (filename);
      return;
    }
  free (filename);

  fstat (fileno (fp), &st);

  tcgetattr (fd, &save_tio);
  line_tio = save_tio;

  /* Wait for NAK or 'C' */
  for (retry = 10; retry > 0; retry--)
    {
      line_tio.c_cc[VMIN] = 0;
      line_tio.c_cc[VTIME] = 20*10;		/* 20 seconds */
      tcsetattr (fd, TCSANOW, &line_tio);
      tcflush (fd, TCIOFLUSH);
      if (read (fd, &ch, 1) == 1 && (ch == NAK || ch == 'C'))
	{
	  /*use_crc = (ch == 'C');*/
	  break;
	}
    }
  if (retry == 0)
    {
      tcsetattr (fd, TCSANOW, &save_tio);
      fprintf (stderr, "abort - receiver not ready\n");
      fclose (fp);
      return;
    }

  /* send packets */
  line_tio.c_cc[VMIN] = 0;
  line_tio.c_cc[VTIME] = 1*10;		/* 1 second */
  tcsetattr (fd, TCSANOW, &line_tio);
  sent = 0;
  for (;;)
    {
      sequence += 1;
      len = fread (buf, 1, packet_size, fp);
      if (!(send_packet (fd, buf, len, packet_size, sequence) == 0 && len > 0))
        break;
      if (sequence % 8 == 0)
	{
	  pct = sent * 100 + 50;
	  pct /= st.st_size;
	  fprintf (stderr, "\r%c % 3ld%%", spinner[(sequence / 8) % 4], pct);
	}
      sent += len;
    }
  tcsetattr (fd, TCSANOW, &save_tio);
  fclose (fp);
  fprintf (stderr, "\r%d packet%s transferred\n",
  	   sequence, (sequence == 1) ? "" : "s");
}

static const unsigned short crc16tab[256] =
  {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
  };

unsigned short
crc16_ccitt (const void *buf, int len)
{
  const unsigned char *p = buf;
  unsigned short crc = 0;
  int counter;

  for (counter = 0; counter < len; counter++)
    crc = (crc << 8) ^ crc16tab[((crc >> 8) ^ *p++) & 0xFF];
  return crc;
}
