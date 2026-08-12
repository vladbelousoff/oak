#include "oak_dap.h"

#include "oak_chunk.h"
#include "oak_module.h"
#include "oak_net.h"
#include "oak_value.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DAP_BUFFER_SIZE 65536
#define DAP_VALUE_REFS  512

typedef struct dap_value_ref dap_value_ref_t;
struct dap_value_ref
{
  oak_value_t value;
};

typedef struct oak_dap oak_dap_t;
struct oak_dap
{
  oak_net_socket_t client;
  int outgoing_seq;
  int configured;
  int stopped;
  char input[DAP_BUFFER_SIZE];
  usize input_len;
  dap_value_ref_t refs[DAP_VALUE_REFS];
  int ref_count;
  const oak_chunk_t* program;
};

static void send_doc(oak_dap_t* dap, yyjson_mut_doc* doc)
{
  size_t len = 0;
  char* json = yyjson_mut_write(doc, 0, &len);
  if (json)
  {
    char header[64];
    const int n =
        snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", len);
    oak_net_send_all(dap->client, header, (usize)n);
    oak_net_send_all(dap->client, json, (usize)len);
    free(json);
  }
  yyjson_mut_doc_free(doc);
}

static yyjson_mut_doc*
message_doc(oak_dap_t* dap, const char* type, yyjson_mut_val** out)
{
  yyjson_mut_doc* doc = yyjson_mut_doc_new(null);
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);
  yyjson_mut_obj_add_int(doc, root, "seq", ++dap->outgoing_seq);
  yyjson_mut_obj_add_str(doc, root, "type", type);
  *out = root;
  return doc;
}

static void send_event(oak_dap_t* dap,
                       const char* event,
                       yyjson_mut_val* body,
                       yyjson_mut_doc* doc)
{
  yyjson_mut_val* root = yyjson_mut_doc_get_root(doc);
  yyjson_mut_obj_add_str(doc, root, "event", event);
  if (body)
    yyjson_mut_obj_add_val(doc, root, "body", body);
  send_doc(dap, doc);
}

static yyjson_mut_doc* response_doc(oak_dap_t* dap,
                                    int request_seq,
                                    const char* command,
                                    yyjson_mut_val** root)
{
  yyjson_mut_doc* doc = message_doc(dap, "response", root);
  yyjson_mut_obj_add_int(doc, *root, "request_seq", request_seq);
  yyjson_mut_obj_add_bool(doc, *root, "success", 1);
  yyjson_mut_obj_add_str(doc, *root, "command", command);
  return doc;
}

static void send_response(oak_dap_t* dap,
                          int request_seq,
                          const char* command,
                          yyjson_mut_val* body,
                          yyjson_mut_doc* doc,
                          yyjson_mut_val* root)
{
  (void)request_seq;
  (void)command;
  if (body)
    yyjson_mut_obj_add_val(doc, root, "body", body);
  send_doc(dap, doc);
}

static void
send_empty_response(oak_dap_t* dap, int request_seq, const char* command)
{
  yyjson_mut_val* root;
  yyjson_mut_doc* doc = response_doc(dap, request_seq, command, &root);
  send_response(dap, request_seq, command, null, doc, root);
}

static yyjson_val* obj_get(yyjson_val* obj, const char* key)
{
  return obj ? yyjson_obj_get(obj, key) : null;
}

static const char* obj_str(yyjson_val* obj, const char* key)
{
  return yyjson_get_str(obj_get(obj, key));
}

static int obj_int(yyjson_val* obj, const char* key)
{
  return yyjson_get_int(obj_get(obj, key));
}

static int obj_bool(yyjson_val* obj, const char* key)
{
  return yyjson_get_bool(obj_get(obj, key));
}

static char source_path_char(const char c)
{
#ifdef _WIN32
  if (c == '\\')
    return '/';
  if (c >= 'A' && c <= 'Z')
    return (char)(c - 'A' + 'a');
#endif
  return c;
}

static int source_matches(const char* a, const char* b)
{
  if (a == b)
    return 1;
  if (!a || !b)
    return 0;
  while (*a && *b)
  {
    if (source_path_char(*a) != source_path_char(*b))
      return 0;
    ++a;
    ++b;
  }
  return *a == *b;
}

static int value_expandable(const oak_value_t value)
{
  return oak_is_array(value) || oak_is_map(value) || oak_is_record(value) ||
         oak_is_interface_object(value);
}

static const char* value_type(const oak_value_t value)
{
  if (oak_is_none_like(value))
    return "none";
  if (oak_is_bool(value))
    return "bool";
  if (oak_is_number(value))
    return "number";
  if (oak_is_string(value))
    return "string";
  if (oak_is_array(value))
    return "array";
  if (oak_is_map(value))
    return "map";
  if (oak_is_record(value))
    return "record";
  if (oak_is_fn(value) || oak_is_native_fn(value))
    return "function";
  return "value";
}

static int add_value_ref(oak_dap_t* dap, oak_value_t value)
{
  if (!value_expandable(value) || dap->ref_count >= DAP_VALUE_REFS)
    return 0;
  dap->refs[dap->ref_count].value = value;
  return ++dap->ref_count;
}

static void add_variable(oak_dap_t* dap,
                         yyjson_mut_doc* doc,
                         yyjson_mut_val* vars,
                         const char* name,
                         oak_value_t value)
{
  char repr[256];
  oak_value_snprint_repr(repr, sizeof(repr), value);
  yyjson_mut_val* var = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_strcpy(doc, var, "name", name);
  yyjson_mut_obj_add_strcpy(doc, var, "value", repr);
  yyjson_mut_obj_add_str(doc, var, "type", value_type(value));
  yyjson_mut_obj_add_int(
      doc, var, "variablesReference", add_value_ref(dap, value));
  yyjson_mut_arr_add_val(vars, var);
}

typedef struct dap_frame dap_frame_t;
struct dap_frame
{
  const oak_chunk_t* chunk;
  usize offset;
  usize stack_base;
  const char* name;
};

static int frame_at(const oak_vm_t* vm, int id, dap_frame_t* out)
{
  if (id < 0 || id > vm->frame_count)
    return 0;
  if (id == 0)
  {
    out->chunk = vm->chunk;
    out->offset = (usize)(vm->ip - oak_chunk_code(vm->chunk));
    out->stack_base = vm->stack_base;
    out->name = "<current>";
    if (vm->frame_count > 0)
    {
      const oak_call_frame_t* fr = &vm->frames[vm->frame_count - 1];
      if (fr->fn_slot < OAK_STACK_MAX && oak_is_fn(vm->stack[fr->fn_slot]))
      {
        const char* name = oak_as_fn(vm->stack[fr->fn_slot])->name;
        if (name)
          out->name = name;
      }
    }
    return 1;
  }

  const int idx = vm->frame_count - id;
  const oak_call_frame_t* fr = &vm->frames[idx];
  out->chunk = fr->return_chunk;
  out->offset = (usize)(fr->return_ip - oak_chunk_code(out->chunk));
  if (out->offset > 0)
    --out->offset;
  out->stack_base = fr->caller_stack_base;
  out->name = "<caller>";
  if (idx > 0)
  {
    const oak_call_frame_t* caller = &vm->frames[idx - 1];
    if (caller->fn_slot < OAK_STACK_MAX &&
        oak_is_fn(vm->stack[caller->fn_slot]))
    {
      const char* name = oak_as_fn(vm->stack[caller->fn_slot])->name;
      if (name)
        out->name = name;
    }
  }
  return 1;
}

static void add_frame_locals(oak_dap_t* dap,
                             const oak_vm_t* vm,
                             int frame_id,
                             yyjson_mut_doc* doc,
                             yyjson_mut_val* vars,
                             const char* only_name)
{
  dap_frame_t frame;
  if (!frame_at(vm, frame_id, &frame) || !frame.chunk->debug)
    return;
  const oak_chunk_debug_t* d = frame.chunk->debug;
  int printed[OAK_STACK_MAX] = { 0 };
  for (usize i = oak_size(d->debug_locals); i > 0; --i)
  {
    const oak_debug_local_t* local = (const oak_debug_local_t*)oak_cget(d->debug_locals, i - 1);
    if (local->slot < 0 || local->slot >= OAK_STACK_MAX ||
        local->offset > frame.offset ||
        (local->end_offset != (usize)-1 && frame.offset >= local->end_offset) ||
        printed[local->slot] ||
        (only_name && strcmp(local->name, only_name) != 0))
      continue;
    const usize idx = frame.stack_base + (usize)local->slot;
    if (idx >= (usize)(vm->sp - vm->stack))
      continue;
    printed[local->slot] = 1;
    add_variable(dap, doc, vars, local->name, vm->stack[idx]);
  }
}

static void handle_set_breakpoints(oak_dap_t* dap,
                                   oak_debugger_t* dbg,
                                   const oak_vm_t* vm,
                                   int seq,
                                   const char* command,
                                   yyjson_val* args)
{
  yyjson_val* source = obj_get(args, "source");
  const char* path = obj_str(source, "path");
  oak_debugger_clear_breakpoints(dbg, path);

  yyjson_mut_val* root;
  yyjson_mut_doc* doc = response_doc(dap, seq, command, &root);
  yyjson_mut_val* body = yyjson_mut_obj(doc);
  yyjson_mut_val* out = yyjson_mut_obj_add_arr(doc, body, "breakpoints");
  yyjson_val* requested = obj_get(args, "breakpoints");
  size_t idx, max;
  yyjson_val* item;
  yyjson_arr_foreach(requested, idx, max, item)
  {
    const int line = obj_int(item, "line");
    int executable = 0;
    const int module_count =
        vm->modules ? (int)oak_size(vm->modules->modules) : 0;
    oak_module_t* const* modules =
        vm->modules ? OAK_DATA(oak_module_t*, vm->modules->modules)
                    : null;
    for (int module_idx = -1; module_idx < module_count && !executable;
         ++module_idx)
    {
      const oak_chunk_t* chunk =
          module_idx < 0 ? dap->program : modules[module_idx]->chunk;
      if (!chunk || !chunk->debug || !chunk->debug->source_name ||
          !source_matches(chunk->debug->source_name, path))
        continue;
      for (usize off = 0; off < oak_chunk_size(chunk); ++off)
      {
        if (oak_chunk_loc(chunk, off).line == line)
        {
          executable = 1;
          break;
        }
      }
    }
    const int id =
        executable ? oak_debugger_add_breakpoint(dbg, line, path) : 0;
    yyjson_mut_val* bp = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, bp, "id", id);
    yyjson_mut_obj_add_bool(doc, bp, "verified", id > 0);
    yyjson_mut_obj_add_int(doc, bp, "line", line);
    yyjson_mut_arr_add_val(out, bp);
  }
  send_response(dap, seq, command, body, doc, root);
}

static void handle_stack_trace(oak_dap_t* dap,
                               const oak_vm_t* vm,
                               int seq,
                               const char* command)
{
  yyjson_mut_val* root;
  yyjson_mut_doc* doc = response_doc(dap, seq, command, &root);
  yyjson_mut_val* body = yyjson_mut_obj(doc);
  yyjson_mut_val* frames = yyjson_mut_obj_add_arr(doc, body, "stackFrames");
  for (int id = 0; id <= vm->frame_count; ++id)
  {
    dap_frame_t frame;
    if (!frame_at(vm, id, &frame))
      continue;
    int line = 0;
    int column = 1;
    const char* source = null;
    if (frame.chunk->debug && frame.offset < oak_chunk_size(frame.chunk))
    {
      line = oak_chunk_loc(frame.chunk, frame.offset).line;
      column = oak_chunk_loc(frame.chunk, frame.offset).column;
      source = frame.chunk->debug->source_name;
    }
    yyjson_mut_val* f = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, f, "id", id);
    yyjson_mut_obj_add_strcpy(doc, f, "name", frame.name);
    yyjson_mut_obj_add_int(doc, f, "line", line);
    yyjson_mut_obj_add_int(doc, f, "column", column > 0 ? column : 1);
    if (source)
    {
      yyjson_mut_val* s = yyjson_mut_obj_add_obj(doc, f, "source");
      yyjson_mut_obj_add_strcpy(doc, s, "name", source);
      yyjson_mut_obj_add_strcpy(doc, s, "path", source);
    }
    yyjson_mut_arr_add_val(frames, f);
  }
  yyjson_mut_obj_add_int(doc, body, "totalFrames", vm->frame_count + 1);
  send_response(dap, seq, command, body, doc, root);
}

static void handle_variables(oak_dap_t* dap,
                             const oak_vm_t* vm,
                             int seq,
                             const char* command,
                             yyjson_val* args)
{
  const int ref = obj_int(args, "variablesReference");
  yyjson_mut_val* root;
  yyjson_mut_doc* doc = response_doc(dap, seq, command, &root);
  yyjson_mut_val* body = yyjson_mut_obj(doc);
  yyjson_mut_val* vars = yyjson_mut_obj_add_arr(doc, body, "variables");

  if (ref >= 1000)
    add_frame_locals(dap, vm, ref - 1000, doc, vars, null);
  else if (ref > 0 && ref <= dap->ref_count)
  {
    const oak_value_t value = dap->refs[ref - 1].value;
    if (oak_is_array(value))
    {
      const oak_obj_array_t* a = oak_as_array(value);
      for (usize i = 0; i < a->length; ++i)
      {
        char name[32];
        snprintf(name, sizeof(name), "[%zu]", (size_t)i);
        add_variable(dap, doc, vars, name, a->items[i]);
      }
    }
    else if (oak_is_map(value))
    {
      const oak_obj_map_t* map = oak_as_map(value);
      for (usize i = 0; i < map->length; ++i)
      {
        char name[256];
        oak_value_snprint_repr(name, sizeof(name), map->entries[i].key);
        add_variable(dap, doc, vars, name, map->entries[i].value);
      }
    }
    else if (oak_is_record(value))
    {
      const oak_obj_record_t* rec = oak_as_record(value);
      for (int i = 0; i < rec->field_count; ++i)
      {
        char fallback[32];
        snprintf(fallback, sizeof(fallback), "[%d]", i);
        add_variable(dap,
                     doc,
                     vars,
                     rec->field_name_ptrs ? rec->field_name_ptrs[i] : fallback,
                     rec->fields[i]);
      }
    }
    else if (oak_is_interface_object(value))
      add_variable(dap, doc, vars, "value", oak_as_interface_object(value)->value);
  }
  send_response(dap, seq, command, body, doc, root);
}

static void handle_request(oak_dap_t* dap,
                           oak_debugger_t* dbg,
                           oak_vm_t* vm,
                           yyjson_val* request)
{
  const int seq = obj_int(request, "seq");
  const char* command = obj_str(request, "command");
  yyjson_val* args = obj_get(request, "arguments");
  if (!command)
    return;

  if (strcmp(command, "initialize") == 0)
  {
    yyjson_mut_val* root;
    yyjson_mut_doc* doc = response_doc(dap, seq, command, &root);
    yyjson_mut_val* body = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, body, "supportsConfigurationDoneRequest", 1);
    yyjson_mut_obj_add_bool(doc, body, "supportsTerminateRequest", 1);
    yyjson_mut_obj_add_bool(doc, body, "supportsEvaluateForHovers", 1);
    send_response(dap, seq, command, body, doc, root);
    yyjson_mut_val* eroot;
    yyjson_mut_doc* edoc = message_doc(dap, "event", &eroot);
    send_event(dap, "initialized", null, edoc);
  }
  else if (strcmp(command, "launch") == 0 || strcmp(command, "attach") == 0)
  {
    dbg->initial_break = obj_bool(args, "stopOnEntry");
    send_empty_response(dap, seq, command);
  }
  else if (strcmp(command, "setBreakpoints") == 0)
    handle_set_breakpoints(dap, dbg, vm, seq, command, args);
  else if (strcmp(command, "configurationDone") == 0)
  {
    dap->configured = 1;
    send_empty_response(dap, seq, command);
  }
  else if (strcmp(command, "threads") == 0)
  {
    yyjson_mut_val* root;
    yyjson_mut_doc* doc = response_doc(dap, seq, command, &root);
    yyjson_mut_val* body = yyjson_mut_obj(doc);
    yyjson_mut_val* threads = yyjson_mut_obj_add_arr(doc, body, "threads");
    yyjson_mut_val* thread = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, thread, "id", 1);
    yyjson_mut_obj_add_str(doc, thread, "name", "Oak VM");
    yyjson_mut_arr_add_val(threads, thread);
    send_response(dap, seq, command, body, doc, root);
  }
  else if (strcmp(command, "stackTrace") == 0)
    handle_stack_trace(dap, vm, seq, command);
  else if (strcmp(command, "scopes") == 0)
  {
    yyjson_mut_val* root;
    yyjson_mut_doc* doc = response_doc(dap, seq, command, &root);
    yyjson_mut_val* body = yyjson_mut_obj(doc);
    yyjson_mut_val* scopes = yyjson_mut_obj_add_arr(doc, body, "scopes");
    yyjson_mut_val* scope = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, scope, "name", "Locals");
    yyjson_mut_obj_add_int(
        doc, scope, "variablesReference", 1000 + obj_int(args, "frameId"));
    yyjson_mut_obj_add_bool(doc, scope, "expensive", 0);
    yyjson_mut_arr_add_val(scopes, scope);
    send_response(dap, seq, command, body, doc, root);
  }
  else if (strcmp(command, "variables") == 0)
    handle_variables(dap, vm, seq, command, args);
  else if (strcmp(command, "evaluate") == 0)
  {
    yyjson_mut_val* root;
    yyjson_mut_doc* doc = response_doc(dap, seq, command, &root);
    yyjson_mut_val* body = yyjson_mut_obj(doc);
    yyjson_mut_val* vars = yyjson_mut_arr(doc);
    add_frame_locals(dap,
                     vm,
                     obj_int(args, "frameId"),
                     doc,
                     vars,
                     obj_str(args, "expression"));
    yyjson_mut_val* first = yyjson_mut_arr_get_first(vars);
    if (first)
    {
      yyjson_mut_obj_add_strcpy(
          doc,
          body,
          "result",
          yyjson_mut_get_str(yyjson_mut_obj_get(first, "value")));
      yyjson_mut_obj_add_int(doc,
                             body,
                             "variablesReference",
                             (int)yyjson_mut_get_int(yyjson_mut_obj_get(
                                 first, "variablesReference")));
    }
    else
    {
      yyjson_mut_obj_add_str(doc, body, "result", "(not found)");
      yyjson_mut_obj_add_int(doc, body, "variablesReference", 0);
    }
    send_response(dap, seq, command, body, doc, root);
  }
  else if (strcmp(command, "continue") == 0 || strcmp(command, "next") == 0 ||
           strcmp(command, "stepIn") == 0 || strcmp(command, "stepOut") == 0)
  {
    if (strcmp(command, "continue") == 0)
      dbg->mode = OAK_DEBUG_MODE_RUN;
    else if (strcmp(command, "next") == 0)
      dbg->mode = OAK_DEBUG_MODE_NEXT;
    else if (strcmp(command, "stepIn") == 0)
      dbg->mode = OAK_DEBUG_MODE_STEP;
    else
      dbg->mode = OAK_DEBUG_MODE_FINISH;
    if (vm->chunk && vm->chunk->debug && vm->ip)
    {
      const usize off = (usize)(vm->ip - oak_chunk_code(vm->chunk));
      dbg->step_line = oak_chunk_loc(vm->chunk, off).line;
      dbg->step_source = vm->chunk->debug->source_name;
      dbg->step_frame_count = vm->frame_count;
    }
    dap->stopped = 0;
    send_empty_response(dap, seq, command);
  }
  else if (strcmp(command, "pause") == 0)
  {
    dbg->pause_requested = 1;
    send_empty_response(dap, seq, command);
  }
  else if (strcmp(command, "disconnect") == 0 ||
           strcmp(command, "terminate") == 0)
  {
    dbg->quit_requested = 1;
    dap->stopped = 0;
    send_empty_response(dap, seq, command);
  }
  else
    send_empty_response(dap, seq, command);
}

static int process_messages(oak_dap_t* dap,
                            oak_debugger_t* dbg,
                            oak_vm_t* vm)
{
  int processed = 0;
  for (;;)
  {
    char* header_end = null;
    for (usize i = 3; i < dap->input_len; ++i)
    {
      if (memcmp(dap->input + i - 3, "\r\n\r\n", 4) == 0)
      {
        header_end = dap->input + i + 1;
        break;
      }
    }
    if (!header_end)
      return processed;
    usize content_length = 0;
    if (sscanf(dap->input, "Content-Length: %zu", &content_length) != 1)
      return -1;
    const usize header_len = (usize)(header_end - dap->input);
    if (header_len + content_length > dap->input_len)
      return processed;
    yyjson_doc* doc = yyjson_read(header_end, content_length, 0);
    if (doc)
    {
      handle_request(dap, dbg, vm, yyjson_doc_get_root(doc));
      yyjson_doc_free(doc);
    }
    const usize consumed = header_len + content_length;
    memmove(dap->input, dap->input + consumed, dap->input_len - consumed);
    dap->input_len -= consumed;
    ++processed;
  }
}

static int receive_messages(oak_dap_t* dap,
                            oak_debugger_t* dbg,
                            oak_vm_t* vm,
                            int block)
{
  const int ready = oak_net_wait_readable(dap->client, block ? -1 : 0);
  if (ready <= 0)
    return ready;
  if (dap->input_len == sizeof(dap->input))
    return -1;
  const int n = oak_net_recv(dap->client,
                             dap->input + dap->input_len,
                             sizeof(dap->input) - dap->input_len);
  if (n <= 0)
    return -1;
  dap->input_len += (usize)n;
  return process_messages(dap, dbg, vm);
}

void oak_dap_poll(oak_debugger_t* dbg, oak_vm_t* vm)
{
  oak_dap_t* dap = dbg->dap_ctx;
  if (!dap)
    return;
  if (receive_messages(dap, dbg, vm, 0) < 0)
    dbg->quit_requested = 1;
}

void oak_dap_stopped(oak_debugger_t* dbg,
                     oak_vm_t* vm,
                     const char* reason)
{
  oak_dap_t* dap = dbg->dap_ctx;
  dap->stopped = 1;
  dap->ref_count = 0;
  yyjson_mut_val* root;
  yyjson_mut_doc* doc = message_doc(dap, "event", &root);
  yyjson_mut_val* body = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_str(doc, body, "reason", reason);
  yyjson_mut_obj_add_int(doc, body, "threadId", 1);
  yyjson_mut_obj_add_bool(doc, body, "allThreadsStopped", 1);
  send_event(dap, "stopped", body, doc);
  while (dap->stopped && !dbg->quit_requested)
  {
    if (receive_messages(dap, dbg, vm, 1) < 0)
      dbg->quit_requested = 1;
  }
}

oak_vm_result_t oak_dap_serve(oak_debugger_t* dbg,
                                   oak_vm_t* vm,
                                   oak_chunk_t* chunk,
                                   int port)
{
  int actual_port = port;
  oak_net_socket_t server = { OAK_NET_INVALID };
  if (!oak_net_init())
    return OAK_VM_RUNTIME_ERROR;
  if (!oak_net_listen_loopback(port, &actual_port, &server))
  {
    oak_net_shutdown();
    return OAK_VM_RUNTIME_ERROR;
  }
  fprintf(stderr, "OAK_DAP_PORT=%d\n", actual_port);
  fflush(stderr);

  oak_dap_t dap;
  memset(&dap, 0, sizeof(dap));
  dap.client.handle = OAK_NET_INVALID;
  dap.program = chunk;
  const int accepted = oak_net_accept(server, &dap.client);
  oak_net_close(server);
  if (!accepted)
  {
    oak_net_shutdown();
    return OAK_VM_RUNTIME_ERROR;
  }

  dbg->dap_mode = 1;
  dbg->dap_ctx = &dap;
  dbg->initial_break = 0;
  while (!dap.configured && !dbg->quit_requested)
  {
    if (receive_messages(&dap, dbg, vm, 1) < 0)
      dbg->quit_requested = 1;
  }

  oak_vm_result_t result = OAK_VM_DEBUG_HALT;
  if (!dbg->quit_requested)
    result = oak_vm_run(vm, chunk);

  yyjson_mut_val* root;
  yyjson_mut_doc* doc = message_doc(&dap, "event", &root);
  yyjson_mut_val* body = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_int(doc, body, "exitCode", result == OAK_VM_OK ? 0 : 1);
  send_event(&dap, "exited", body, doc);
  doc = message_doc(&dap, "event", &root);
  send_event(&dap, "terminated", null, doc);

  dbg->dap_ctx = null;
  oak_net_close(dap.client);
  oak_net_shutdown();
  return result;
}
