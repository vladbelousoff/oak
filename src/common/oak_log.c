#include "oak_log.h"

#include "oak_types.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

const char* oak_path_basename(const char* path)
{
#if defined(_WIN32)
  const char* p = strrchr(path, '\\');
  if (!p)
    p = strrchr(path, '/');
#else
  const char* p = strrchr(path, '/');
#endif
  return p ? p + 1 : path;
}

static const char* oak_time_stamp(void)
{
  static _Thread_local char buf[16];
  time_t now = time(null);
  struct tm tmv;

#if defined(_WIN32)
  tmv = *localtime(&now);
#else
  localtime_r(&now, &tmv);
#endif

  strftime(buf, sizeof(buf), "%H:%M:%S", &tmv);
  return buf;
}

static char* oak_log_lvl_to_str(const enum oak_log_level_t lvl)
{
  switch (lvl)
  {
    case OAK_LOG_ERROR:
      return "ERR";
    case OAK_LOG_WARN:
      return "WRN";
    case OAK_LOG_DEBUG:
      return "DBG";
    default:
      break;
  }

  return "INF";
}

void _oak_log_printf(const enum oak_log_level_t lvl,
#ifdef OAK_DEBUG_LOGGING
                     const char* file,
                     const unsigned line,
#endif
                     const char* fmt,
                     ...)
{
  static _Thread_local char buf[4096];

  int off = 0;
#ifdef OAK_DEBUG_LOGGING
  if (lvl != OAK_LOG_INFO)
  {
    off = snprintf(buf,
                   sizeof(buf),
                   "%s|%s %s:%u ",
                   oak_log_lvl_to_str(lvl),
                   oak_time_stamp(),
                   oak_path_basename(file),
                   line);
  }

  if (off < 0)
    return;
#endif

  usize pos = (usize)off;
  if (pos >= sizeof(buf))
    pos = sizeof(buf) - 1;

  va_list ap;
  va_start(ap, fmt);
  const int msg = vsnprintf(buf + pos, sizeof(buf) - pos, fmt, ap);
  va_end(ap);

  if (msg > 0)
    pos += (usize)msg;

  if (pos >= sizeof(buf) - 1)
    pos = sizeof(buf) - 2;

  buf[pos] = '\n';
  buf[pos + 1] = '\0';

  fputs(buf, stdout);
}
