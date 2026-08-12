#include "internal/oak_compiler.h"
#include "oak_vm.h"

oak_fn_call_result_t builtin_size(oak_native_ctx_t* ctx,
                                       const oak_value_t* args,
                                       int argc,
                                       oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (oak_is_array(args[0]))
  {
    *out_result = OAK_VALUE_I32((int)oak_as_array(args[0])->length);
    return OAK_FN_CALL_OK;
  }
  if (oak_is_map(args[0]))
  {
    *out_result = OAK_VALUE_I32((int)oak_as_map(args[0])->length);
    return OAK_FN_CALL_OK;
  }
  if (oak_is_string(args[0]))
  {
    *out_result = OAK_VALUE_I32((int)oak_as_string(args[0])->length);
    return OAK_FN_CALL_OK;
  }
  return OAK_FN_CALL_RUNTIME_ERROR;
}

oak_fn_call_result_t builtin_push(oak_native_ctx_t* ctx,
                                       const oak_value_t* args,
                                       int argc,
                                       oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 2 || !oak_is_array(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  if (!oak_array_push(oak_as_array(args[0]), args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_I32((int)oak_as_array(args[0])->length);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t builtin_has(oak_native_ctx_t* ctx,
                                      const oak_value_t* args,
                                      int argc,
                                      oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 2 || !oak_is_map(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const int found = oak_map_has(oak_as_map(args[0]), args[1]);
  *out_result = OAK_VALUE_BOOL(found);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t builtin_delete(oak_native_ctx_t* ctx,
                                         const oak_value_t* args,
                                         int argc,
                                         oak_value_t* out_result)
{
  (void)ctx;
  if (argc != 2 || !oak_is_map(args[0]))
    return OAK_FN_CALL_RUNTIME_ERROR;
  const int removed = oak_map_delete(oak_as_map(args[0]), args[1]);
  *out_result = OAK_VALUE_BOOL(removed);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t builtin_to_string(oak_native_ctx_t* ctx,
                                            const oak_value_t* args,
                                            int argc,
                                            oak_value_t* out_result)
{
  if (argc != 1)
    return OAK_FN_CALL_RUNTIME_ERROR;
  oak_obj_string_t* s = oak_vm_value_to_string(ctx->vm, args[0]);
  if (!s)
    return OAK_FN_CALL_RUNTIME_ERROR;
  *out_result = OAK_VALUE_OBJ(s);
  return OAK_FN_CALL_OK;
}

oak_fn_call_result_t builtin_string_format(oak_native_ctx_t* ctx,
                                                const oak_value_t* args,
                                                int argc,
                                                oak_value_t* out_result)
{
  if (argc != 2 || !oak_is_string(args[0]) || !oak_is_array(args[1]))
    return OAK_FN_CALL_RUNTIME_ERROR;

  const oak_obj_string_t* tmpl = oak_as_string(args[0]);
  const oak_obj_array_t* subs = oak_as_array(args[1]);

  oak_obj_string_t* acc = oak_vm_string_new_len(ctx->vm, "", 0);
  const char* s = tmpl->chars;
  const usize len = tmpl->length;
  usize i = 0;
  usize implicit = 0;

  while (i < len)
  {
    if (s[i] == '{')
    {
      if (i + 1 < len && s[i + 1] == '{')
      {
        oak_obj_string_t* lit = oak_vm_string_new_len(ctx->vm, "{", 1);
        oak_obj_string_t* next = oak_vm_string_concat(ctx->vm, acc, lit);
        oak_value_decref(OAK_VALUE_OBJ(acc));
        oak_value_decref(OAK_VALUE_OBJ(lit));
        acc = next;
        i += 2;
        continue;
      }

      ++i;
      usize idx;
      if (i < len && s[i] == '}')
      {
        idx = implicit++;
        ++i;
      }
      else if (i < len && s[i] >= '0' && s[i] <= '9')
      {
        idx = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9')
        {
          idx = idx * 10u + (usize)(s[i] - '0');
          ++i;
        }
        if (i >= len || s[i] != '}')
        {
          oak_value_decref(OAK_VALUE_OBJ(acc));
          return OAK_FN_CALL_RUNTIME_ERROR;
        }
        ++i;
      }
      else
      {
        oak_value_decref(OAK_VALUE_OBJ(acc));
        return OAK_FN_CALL_RUNTIME_ERROR;
      }

      if (idx >= subs->length)
      {
        oak_value_decref(OAK_VALUE_OBJ(acc));
        return OAK_FN_CALL_RUNTIME_ERROR;
      }

      oak_obj_string_t* piece =
          oak_vm_string_from_value_repr(ctx->vm, subs->items[idx]);
      if (!piece)
      {
        oak_value_decref(OAK_VALUE_OBJ(acc));
        return OAK_FN_CALL_RUNTIME_ERROR;
      }
      oak_obj_string_t* next = oak_vm_string_concat(ctx->vm, acc, piece);
      oak_value_decref(OAK_VALUE_OBJ(acc));
      oak_value_decref(OAK_VALUE_OBJ(piece));
      acc = next;
      continue;
    }

    if (i + 1 < len && s[i] == '}' && s[i + 1] == '}')
    {
      oak_obj_string_t* lit = oak_vm_string_new_len(ctx->vm, "}", 1);
      oak_obj_string_t* next = oak_vm_string_concat(ctx->vm, acc, lit);
      oak_value_decref(OAK_VALUE_OBJ(acc));
      oak_value_decref(OAK_VALUE_OBJ(lit));
      acc = next;
      i += 2;
      continue;
    }

    {
      const usize start = i;
      while (i < len)
      {
        if (s[i] == '{')
          break;
        if (s[i] == '}' && i + 1 < len && s[i + 1] == '}')
          break;
        ++i;
      }
      oak_obj_string_t* lit =
          oak_vm_string_new_len(ctx->vm, s + start, i - start);
      oak_obj_string_t* next = oak_vm_string_concat(ctx->vm, acc, lit);
      oak_value_decref(OAK_VALUE_OBJ(acc));
      oak_value_decref(OAK_VALUE_OBJ(lit));
      acc = next;
    }
  }

  *out_result = OAK_VALUE_OBJ(acc);
  return OAK_FN_CALL_OK;
}
