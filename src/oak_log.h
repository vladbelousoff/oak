#pragma once

const char* oak_filename(const char* path);

typedef enum
{
  OAK_LOG_INF,
  OAK_LOG_DBG,
  OAK_LOG_WRN,
  OAK_LOG_ERR,
} oak_log_level_t;

void _oak_log_printf(oak_log_level_t lvl,
#ifdef OAK_DEBUG_LOGGING
                     const char* file,
                     unsigned line,
#endif
                     const char* fmt,
                     ...);

#ifdef OAK_DEBUG_LOGGING

#define oak_log(lvl, fmt, ...)                                                 \
  do                                                                           \
  {                                                                            \
    _oak_log_printf(lvl, __FILE__, __LINE__, fmt, ##__VA_ARGS__);              \
  } while (0)

#define oak_log_cond(cond, lvl, fmt, ...)                                      \
  do                                                                           \
  {                                                                            \
    if (cond)                                                                  \
    {                                                                          \
      _oak_log_printf(lvl, __FILE__, __LINE__, fmt, ##__VA_ARGS__);            \
    }                                                                          \
  } while (0)

#else

#define oak_log(lvl, fmt, ...)                                                 \
  do                                                                           \
  {                                                                            \
    if (lvl == OAK_LOG_ERR)                                                    \
    {                                                                          \
      _oak_log_printf(lvl, fmt, ##__VA_ARGS__);                                \
    }                                                                          \
  } while (0)

#define oak_log_cond(cond, lvl, fmt, ...)                                      \
  do                                                                           \
  {                                                                            \
    if (lvl == OAK_LOG_ERR && (cond))                                          \
    {                                                                          \
      _oak_log_printf(lvl, fmt, ##__VA_ARGS__);                                \
    }                                                                          \
  } while (0)

#endif
