#pragma once

#include "oak_types.h"

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

static inline void oak_list_init(struct oak_list_entry_t* head)
{
  head->prev = head;
  head->next = head;
}

static inline int oak_list_empty(const struct oak_list_entry_t* head)
{
  return head->next == head;
}

static void _oak_list_insert(struct oak_list_entry_t* _new,
                             struct oak_list_entry_t* prev,
                             struct oak_list_entry_t* next)
{
  next->prev = _new;
  _new->next = next;
  _new->prev = prev;
  prev->next = _new;
}

static inline void oak_list_add_head(struct oak_list_entry_t* head,
                                     struct oak_list_entry_t* entry)
{
  _oak_list_insert(entry, head, head->next);
}

static inline void oak_list_add_tail(struct oak_list_entry_t* head,
                                     struct oak_list_entry_t* entry)
{
  _oak_list_insert(entry, head->prev, head);
}

static void _oak_list_remove(struct oak_list_entry_t* prev,
                             struct oak_list_entry_t* next)
{
  next->prev = prev;
  prev->next = next;
}

static inline void oak_list_remove(const struct oak_list_entry_t* entry)
{
  _oak_list_remove(entry->prev, entry->next);
}

static inline struct oak_list_entry_t*
oak_list_first(const struct oak_list_entry_t* head)
{
  if (oak_list_empty(head))
    return null;
  return head->next;
}

static inline struct oak_list_entry_t*
oak_list_next(const struct oak_list_entry_t* current,
              const struct oak_list_entry_t* head)
{
  if (current == null || current->next == head)
    return null;
  return current->next;
}

static inline void oak_list_move(const struct oak_list_entry_t* from,
                                 struct oak_list_entry_t* to)
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

static inline usize oak_list_length(const struct oak_list_entry_t* head)
{
  usize length = 0;
  struct oak_list_entry_t* current;
  oak_list_for_each_indexed(length, current, head)
  {
  }
  return length;
}
