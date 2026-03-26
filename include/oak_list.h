#pragma once

#include <stddef.h>

#define oak_container_of(address, type, field)                                 \
  ((type*)((char*)(address) - (char*)(&((type*)0)->field)))

#define oak_list_for_each(position, head)                                      \
  for (position = (head)->next; position != head; position = position->next)

#define oak_list_for_each_safe(position, n, head)                              \
  for (position = (head)->next, n = position->next; position != (head);        \
       position = n, n = position->next)

#define oak_list_for_each_indexed(index, position, head)                       \
  for (position = (head)->next, index = 0; position != (head);                 \
       position = position->next, ++index)

struct oak_list_entry_t
{
  struct oak_list_entry_t* prev;
  struct oak_list_entry_t* next;
};

typedef struct oak_list_entry_t oak_list_entry_t;
typedef oak_list_entry_t oak_list_head_t;

inline void oak_list_init(oak_list_entry_t* head)
{
  head->prev = head;
  head->next = head;
}

inline int oak_list_empty(const oak_list_entry_t* head)
{
  return head->next == head;
}

static void _oak_list_insert(oak_list_entry_t* _new,
                             oak_list_entry_t* prev,
                             oak_list_entry_t* next)
{
  next->prev = _new;
  _new->next = next;
  _new->prev = prev;
  prev->next = _new;
}

inline void oak_list_add_head(oak_list_entry_t* head, oak_list_entry_t* entry)
{
  _oak_list_insert(entry, head, head->next);
}

inline void oak_list_add_tail(oak_list_entry_t* head, oak_list_entry_t* entry)
{
  _oak_list_insert(entry, head->prev, head);
}

static void _oak_list_remove(oak_list_entry_t* prev, oak_list_entry_t* next)
{
  next->prev = prev;
  prev->next = next;
}

inline void oak_list_remove(const oak_list_entry_t* entry)
{
  _oak_list_remove(entry->prev, entry->next);
}

inline oak_list_entry_t* oak_list_first(const oak_list_entry_t* head)
{
  if (oak_list_empty(head))
    return NULL;
  return head->next;
}

inline oak_list_entry_t* oak_list_next(const oak_list_entry_t* current,
                                       const oak_list_entry_t* head)
{
  if (current == NULL || current->next == head)
    return NULL;
  return current->next;
}

inline void oak_list_move(const oak_list_entry_t* from, oak_list_entry_t* to)
{
  if (!oak_list_empty(from))
  {
    *to = *from;
    to->next->prev = to;
    to->prev->next = to;
  }
  else
  {
    oak_list_init(to);
  }
}

inline size_t oak_list_length(const oak_list_entry_t* head)
{
  size_t length = 0;
  oak_list_entry_t* current;
  oak_list_for_each_indexed(length, current, head)
  {
  }
  return length;
}
