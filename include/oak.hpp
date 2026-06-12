#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

extern "C" {
#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_module.h"
#include "oak_module_loader.h"
#include "oak_parser.h"
#include "oak_stdlib.h"
#include "oak_vm.h"
}

namespace oak {

template<typename T>
class TypeBuilder;

namespace detail {

// ---------------------------------------------------------------------------
// Closure storage: C++ callables are heap-allocated and handed to the C API
// through the binding user_data pointers, so any number of bindings works.
// CompileOptions owns the closures and must outlive every chunk/VM compiled
// with it — the same lifetime the C API already requires of bind descriptors.
// ---------------------------------------------------------------------------

using native_fn_closure =
    std::function<oak_fn_call_result_t(oak_native_ctx_t*, const oak_value_t*,
                                       int, oak_value_t*)>;

struct field_closure
{
  std::function<oak_value_t(oak_value_t)> get;
  std::function<void(oak_value_t, oak_value_t)> set;
};

inline oak_fn_call_result_t native_fn_bridge(oak_native_ctx_t* ctx,
                                             const oak_value_t* args,
                                             int argc, oak_value_t* out)
{
  return (*static_cast<native_fn_closure*>(ctx->user_data))(ctx, args, argc,
                                                            out);
}

inline oak_value_t field_getter_bridge(oak_value_t self, void* user_data)
{
  return static_cast<field_closure*>(user_data)->get(self);
}

inline void field_setter_bridge(oak_value_t self, oak_value_t value,
                                void* user_data)
{
  static_cast<field_closure*>(user_data)->set(self, value);
}

// ---------------------------------------------------------------------------
// Type trait helpers
// ---------------------------------------------------------------------------

template<typename T>
inline constexpr oak_type_id_t oak_type_for = OAK_TYPE_VOID;
template<>
inline constexpr oak_type_id_t oak_type_for<int> = OAK_TYPE_NUMBER;
template<>
inline constexpr oak_type_id_t oak_type_for<float> = OAK_TYPE_NUMBER;
template<>
inline constexpr oak_type_id_t oak_type_for<bool> = OAK_TYPE_BOOL;

template<typename T> oak_value_t to_oak_value(T val);
template<> inline oak_value_t to_oak_value<int>(int v) { return oak_value_i32(static_cast<i32>(v)); }
template<> inline oak_value_t to_oak_value<float>(float v) { return oak_value_f32(v); }
template<> inline oak_value_t to_oak_value<bool>(bool v) { return oak_value_bool(v ? 1 : 0); }

// Oak numbers are runtime-tagged i32 OR f32 and the VM passes them to native
// callbacks uncoerced, so numeric extraction must accept either tag (same
// pattern as the stdlib builtins).
inline int number_as_i32(oak_value_t v)
{
  return oak_is_f32(v) ? static_cast<int>(oak_as_f32(v)) : oak_as_i32(v);
}

inline float number_as_f32(oak_value_t v)
{
  return oak_is_f32(v) ? oak_as_f32(v) : static_cast<float>(oak_as_i32(v));
}

template<typename T> T from_oak_value(oak_value_t v);
template<> inline int from_oak_value<int>(oak_value_t v) { return number_as_i32(v); }
template<> inline float from_oak_value<float>(oak_value_t v) { return number_as_f32(v); }
template<> inline bool from_oak_value<bool>(oak_value_t v) { return oak_as_bool(v) != 0; }

} // namespace detail

// ===========================================================================
// Allocator
// ===========================================================================

class Allocator
{
  oak_allocator_t alloc_{};

public:
  enum kind
  {
    system,
    tracking
  };

  explicit Allocator(kind k = system)
  {
    if (k == tracking)
      oak_tracking_allocator_init(&alloc_);
    else
      oak_system_allocator_init(&alloc_);
  }

  ~Allocator()
  {
    if (alloc_.shutdown)
      alloc_.shutdown(&alloc_);
  }

  Allocator(const Allocator&) = delete;
  Allocator& operator=(const Allocator&) = delete;
  Allocator(Allocator&&) = delete;
  Allocator& operator=(Allocator&&) = delete;

  oak_allocator_t* get() noexcept { return &alloc_; }
  const oak_allocator_t* get() const noexcept { return &alloc_; }
};

// ===========================================================================
// Value
// ===========================================================================

class Value
{
  oak_value_t val_;

public:
  Value() noexcept : val_(oak_value_none()) {}

  static Value from_raw(oak_value_t v) noexcept
  {
    Value r;
    r.val_ = v;
    return r;
  }

  static Value i32(std::int32_t i) noexcept
  {
    return from_raw(oak_value_i32(static_cast<::i32>(i)));
  }
  static Value f32(float f) noexcept { return from_raw(oak_value_f32(f)); }
  static Value boolean(bool b) noexcept
  {
    return from_raw(oak_value_bool(b ? 1 : 0));
  }
  static Value none() noexcept { return from_raw(oak_value_none()); }
  static Value handle(std::uint64_t h) noexcept
  {
    return from_raw(oak_value_handle(static_cast<u64>(h)));
  }
  static Value string(Allocator& alloc, const char* s)
  {
    oak_obj_string_t* str = oak_string_new(alloc.get(), s);
    return from_raw(OAK_VALUE_OBJ(str));
  }

  Value(int i) noexcept : val_(oak_value_i32(static_cast<::i32>(i))) {}
  Value(float f) noexcept : val_(oak_value_f32(f)) {}
  Value(double d) noexcept : val_(oak_value_f32(static_cast<float>(d))) {}
  Value(bool b) noexcept : val_(oak_value_bool(b ? 1 : 0)) {}
  // Without this, a pointer would silently convert to bool. Strings go
  // through Value::string().
  template<typename P> Value(P*) = delete;

  Value(const Value& other) noexcept : val_(other.val_)
  {
    oak_value_incref(val_);
  }

  Value& operator=(const Value& other) noexcept
  {
    oak_value_incref(other.val_);
    oak_value_decref(val_);
    val_ = other.val_;
    return *this;
  }

  Value(Value&& other) noexcept : val_(other.val_)
  {
    other.val_ = oak_value_none();
  }

  Value& operator=(Value&& other) noexcept
  {
    if (this != &other)
    {
      oak_value_decref(val_);
      val_ = other.val_;
      other.val_ = oak_value_none();
    }
    return *this;
  }

  ~Value() { oak_value_decref(val_); }

  // Type predicates
  bool is_i32() const noexcept { return oak_is_i32(val_) != 0; }
  bool is_f32() const noexcept { return oak_is_f32(val_) != 0; }
  bool is_bool() const noexcept { return oak_is_bool(val_) != 0; }
  bool is_none() const noexcept { return oak_is_none(val_) != 0; }
  bool is_number() const noexcept { return oak_is_number(val_) != 0; }
  bool is_string() const noexcept { return oak_is_string(val_) != 0; }
  bool is_array() const noexcept { return oak_is_array(val_) != 0; }
  bool is_map() const noexcept { return oak_is_map(val_) != 0; }
  bool is_fn() const noexcept { return oak_is_fn(val_) != 0; }
  bool is_record() const noexcept { return oak_is_record(val_) != 0; }
  bool is_native_record() const noexcept
  {
    return oak_is_native_record(val_) != 0;
  }
  bool is_handle() const noexcept { return oak_is_handle(val_) != 0; }
  bool is_truthy() const noexcept { return oak_is_truthy(val_) != 0; }

  // Typed extractors. The numeric ones coerce between i32/f32 because Oak
  // number values carry either tag at runtime.
  std::int32_t as_i32() const
  {
    return static_cast<std::int32_t>(detail::number_as_i32(val_));
  }
  float as_f32() const { return detail::number_as_f32(val_); }
  bool as_bool() const { return oak_as_bool(val_) != 0; }
  std::uint64_t as_handle() const
  {
    return static_cast<std::uint64_t>(oak_value_as_handle(val_));
  }

  std::string_view as_string() const
  {
    oak_obj_string_t* s = oak_as_string(val_);
    return {s->chars, s->length};
  }

  void* as_native_instance() const { return oak_native_instance(val_); }

  // Raw access
  oak_value_t raw() const noexcept { return val_; }

  oak_value_t release() noexcept
  {
    oak_value_t v = val_;
    val_ = oak_value_none();
    return v;
  }

  bool operator==(const Value& other) const noexcept
  {
    return oak_value_equal(val_, other.val_) != 0;
  }
};

// ===========================================================================
// Args — non-owning view over native function arguments
// ===========================================================================

class Args
{
  std::span<const oak_value_t> data_;

public:
  Args(const oak_value_t* data, int count) noexcept
      : data_(data, static_cast<std::size_t>(count))
  {
  }

  int size() const noexcept { return static_cast<int>(data_.size()); }

  Value operator[](std::size_t i) const
  {
    oak_value_incref(data_[i]);
    return Value::from_raw(data_[i]);
  }

  oak_value_t raw(std::size_t i) const noexcept { return data_[i]; }
};

// ===========================================================================
// Context — wraps oak_native_ctx_t*
// ===========================================================================

class Context
{
  oak_native_ctx_t* ctx_;

public:
  explicit Context(oak_native_ctx_t* ctx) noexcept : ctx_(ctx) {}

  oak_vm_t* vm() noexcept { return ctx_->vm; }
  oak_allocator_t* allocator() noexcept { return ctx_->allocator; }
  void* user_data() noexcept { return ctx_->vm->user_data; }
  oak_native_ctx_t* raw() noexcept { return ctx_; }
};

namespace detail {

// Adapts a C++ callable (Context&, Args) -> Value to the C native fn ABI.
template<typename F>
native_fn_closure adapt_native_fn(F&& fn)
{
  return [f = std::forward<F>(fn)](oak_native_ctx_t* ctx, const oak_value_t* a,
                                   int argc,
                                   oak_value_t* out) -> oak_fn_call_result_t {
    Context c(ctx);
    Args args_view(a, argc);
    Value result = f(c, args_view);
    *out = result.release();
    return OAK_FN_CALL_OK;
  };
}

} // namespace detail

// ===========================================================================
// Diagnostic — lightweight view
// ===========================================================================

class Diagnostic
{
  const oak_diagnostic_t* d_;

public:
  explicit Diagnostic(const oak_diagnostic_t* d) noexcept : d_(d) {}

  int line() const noexcept { return d_->line; }
  int column() const noexcept { return d_->column; }
  const char* message() const noexcept { return d_->message; }
};

// ===========================================================================
// AST — non-owning views over nodes held by ParseResult
// ===========================================================================

class AstNode;

class AstNodeIterator
{
  const oak_ast_node_t* parent_ = null;
  std::size_t index_ = 0;

public:
  using difference_type = std::ptrdiff_t;
  using value_type = AstNode;

  AstNodeIterator() noexcept = default;
  AstNodeIterator(const oak_ast_node_t* parent, std::size_t index) noexcept
      : parent_(parent), index_(index)
  {
  }

  AstNode operator*() const noexcept;

  AstNodeIterator& operator++() noexcept
  {
    ++index_;
    return *this;
  }

  AstNodeIterator operator++(int) noexcept
  {
    AstNodeIterator old = *this;
    ++*this;
    return old;
  }

  bool operator==(const AstNodeIterator&) const noexcept = default;
};

class AstNodeChildren
{
  const oak_ast_node_t* parent_ = null;

public:
  explicit AstNodeChildren(const oak_ast_node_t* parent) noexcept
      : parent_(parent)
  {
  }

  AstNodeIterator begin() const noexcept { return {parent_, 0}; }
  AstNodeIterator end() const noexcept
  {
    return {parent_, oak_ast_node_child_count(parent_)};
  }
  std::size_t size() const noexcept
  {
    return oak_ast_node_child_count(parent_);
  }
  bool empty() const noexcept { return size() == 0; }
};

class AstNode
{
  const oak_ast_node_t* node_ = null;

public:
  AstNode() noexcept = default;
  explicit AstNode(const oak_ast_node_t* node) noexcept : node_(node) {}

  explicit operator bool() const noexcept { return node_ != null; }

  oak_node_kind_t kind() const noexcept
  {
    return node_ ? node_->kind : OAK_NODE_NONE;
  }
  const char* kind_name() const noexcept
  {
    return oak_ast_node_kind_name(kind());
  }

  bool is_unary() const noexcept { return oak_node_is_unary_op(kind()) != 0; }
  bool is_binary() const noexcept
  {
    return oak_node_is_binary_op(kind()) != 0;
  }
  bool is_terminal() const noexcept
  {
    return oak_node_is_token_terminal(kind()) != 0;
  }

  std::size_t child_count() const noexcept
  {
    return oak_ast_node_child_count(node_);
  }
  AstNode child(std::size_t index) const noexcept
  {
    return AstNode(oak_ast_node_child_at(node_, index));
  }
  AstNodeChildren children() const noexcept { return AstNodeChildren(node_); }

  const oak_token_t* token() const noexcept
  {
    return is_terminal() ? node_->token : null;
  }
  oak_token_kind_t token_kind() const noexcept
  {
    const oak_token_t* t = token();
    return t ? oak_token_kind(t) : OAK_TOKEN_IDENT;
  }
  std::string_view text() const noexcept
  {
    const oak_token_t* t = token();
    return t ? std::string_view(oak_token_text(t),
                                static_cast<std::size_t>(oak_token_size(t)))
             : std::string_view{};
  }
  int line() const noexcept
  {
    const oak_token_t* t = token();
    return t ? oak_token_line(t) : 0;
  }
  int column() const noexcept
  {
    const oak_token_t* t = token();
    return t ? oak_token_column(t) : 0;
  }
  int offset() const noexcept
  {
    const oak_token_t* t = token();
    return t ? oak_token_offset(t) : 0;
  }

  const oak_ast_node_t* raw() const noexcept { return node_; }
};

inline AstNode AstNodeIterator::operator*() const noexcept
{
  return AstNode(oak_ast_node_child_at(parent_, index_));
}

template<typename F>
void walk(AstNode root, F&& visitor)
{
  if (!root)
    return;

  std::vector<AstNode> pending{root};
  while (!pending.empty())
  {
    AstNode node = pending.back();
    pending.pop_back();
    std::invoke(visitor, node);

    for (std::size_t i = node.child_count(); i > 0; --i)
      pending.push_back(node.child(i - 1));
  }
}

// ===========================================================================
// ParseResult
// ===========================================================================

class ParseResult
{
  oak_lexer_result_t* lexer_ = null;
  oak_parser_result_t result_{};

  ParseResult(oak_lexer_result_t* lexer, oak_parser_result_t result) noexcept
      : lexer_(lexer), result_(result)
  {
  }

  friend ParseResult parse(const char*, Allocator&, oak_node_kind_t);

public:
  ParseResult() noexcept = default;

  ~ParseResult()
  {
    oak_parser_free(&result_);
    oak_lexer_free(lexer_);
  }

  ParseResult(const ParseResult&) = delete;
  ParseResult& operator=(const ParseResult&) = delete;

  ParseResult(ParseResult&& other) noexcept
      : lexer_(other.lexer_), result_(other.result_)
  {
    other.lexer_ = null;
    other.result_ = {};
  }

  ParseResult& operator=(ParseResult&& other) noexcept
  {
    if (this != &other)
    {
      oak_parser_free(&result_);
      oak_lexer_free(lexer_);
      lexer_ = other.lexer_;
      result_ = other.result_;
      other.lexer_ = null;
      other.result_ = {};
    }
    return *this;
  }

  bool ok() const noexcept
  {
    return root() && lexer_error_count() == 0 && error_count() == 0;
  }
  AstNode root() const noexcept { return AstNode(oak_parser_root(&result_)); }
  int lexer_error_count() const noexcept
  {
    return lexer_ ? oak_lexer_error_count(lexer_) : 0;
  }
  int error_count() const noexcept
  {
    return oak_parser_error_count(&result_);
  }
  Diagnostic error(int i) const
  {
    return Diagnostic(&oak_parser_errors(&result_)[i]);
  }

  const oak_lexer_result_t* raw_lexer() const noexcept { return lexer_; }
  const oak_parser_result_t* raw() const noexcept { return &result_; }
};

// ===========================================================================
// CompileResult
// ===========================================================================

class CompileResult
{
  oak_compile_result_t result_{};
  bool owned_ = true;

public:
  CompileResult() noexcept = default;

  ~CompileResult()
  {
    if (owned_)
      oak_compile_result_free(&result_);
  }

  CompileResult(const CompileResult&) = delete;
  CompileResult& operator=(const CompileResult&) = delete;

  CompileResult(CompileResult&& other) noexcept
      : result_(other.result_), owned_(other.owned_)
  {
    other.owned_ = false;
    other.result_ = {};
  }

  CompileResult& operator=(CompileResult&& other) noexcept
  {
    if (this != &other)
    {
      if (owned_)
        oak_compile_result_free(&result_);
      result_ = other.result_;
      owned_ = other.owned_;
      other.owned_ = false;
      other.result_ = {};
    }
    return *this;
  }

  bool ok() const noexcept { return result_.chunk != null; }
  oak_chunk_t* chunk() noexcept { return result_.chunk; }
  int error_count() const noexcept { return result_.error_count; }

  Diagnostic error(int i) const
  {
    return Diagnostic(&result_.errors[i]);
  }

  oak_compile_result_t* raw() noexcept { return &result_; }
};

// ===========================================================================
// CompileOptions
// ===========================================================================

class CompileOptions
{
  oak_compile_options_t opts_{};

public:
  explicit CompileOptions(Allocator& alloc)
  {
    oak_compile_options_init(&opts_, alloc.get());
  }

  ~CompileOptions() { oak_compile_options_free(&opts_); }

  CompileOptions(const CompileOptions&) = delete;
  CompileOptions& operator=(const CompileOptions&) = delete;
  CompileOptions(CompileOptions&&) = delete;
  CompileOptions& operator=(CompileOptions&&) = delete;

  CompileOptions& source_name(const char* name)
  {
    opts_.source_name = name;
    return *this;
  }

  CompileOptions& debug_info(bool enable)
  {
    opts_.emit_debug_info = enable ? 1 : 0;
    return *this;
  }

  CompileOptions& with_stdlib()
  {
    oak_stdlib_register(&opts_);
    return *this;
  }

  // --- Native function binding ---

  template<typename F>
  CompileOptions& bind_fn(const char* name, int arity, F&& fn,
                           oak_bind_type_ref_t return_type =
                               OAK_BIND_SCALAR(OAK_TYPE_VOID))
  {
    return bind_fn(null, name, arity, std::forward<F>(fn), return_type);
  }

  template<typename F>
  CompileOptions& bind_fn(const char* module, const char* name, int arity,
                           F&& fn,
                           oak_bind_type_ref_t return_type =
                               OAK_BIND_SCALAR(OAK_TYPE_VOID))
  {
    oak_bind_global_fn_t desc{};
    desc.module_name = module;
    desc.name = name;
    desc.impl = detail::native_fn_bridge;
    desc.user_data =
        store_fn_closure(detail::adapt_native_fn(std::forward<F>(fn)));
    desc.arity = arity;
    desc.return_type = return_type;
    oak_bind_fn_global(&opts_, &desc);
    return *this;
  }

  // Raw C callback overload. `user_data` is surfaced to the callback as
  // oak_native_ctx_t::user_data.
  CompileOptions& bind_fn_raw(const char* name, int arity,
                               oak_native_fn_t fn,
                               oak_bind_type_ref_t return_type =
                                   OAK_BIND_SCALAR(OAK_TYPE_VOID),
                               void* user_data = nullptr)
  {
    return bind_fn_raw(null, name, arity, fn, return_type, user_data);
  }

  CompileOptions& bind_fn_raw(const char* module, const char* name,
                               int arity, oak_native_fn_t fn,
                               oak_bind_type_ref_t return_type =
                                   OAK_BIND_SCALAR(OAK_TYPE_VOID),
                               void* user_data = nullptr)
  {
    oak_bind_global_fn_t desc{};
    desc.module_name = module;
    desc.name = name;
    desc.impl = fn;
    desc.user_data = user_data;
    desc.arity = arity;
    desc.return_type = return_type;
    oak_bind_fn_global(&opts_, &desc);
    return *this;
  }

  // --- Native type binding ---

  template<typename T>
  TypeBuilder<T> bind_type(const char* name,
                           oak_bind_type_kind_t kind = OAK_BIND_TYPE_RECORD)
  {
    oak_bind_type_t* t = oak_bind_type(&opts_, kind, name);
    return TypeBuilder<T>(*this, t);
  }

  template<typename T>
  TypeBuilder<T> bind_type(const char* module, const char* name,
                           oak_bind_type_kind_t kind = OAK_BIND_TYPE_RECORD)
  {
    oak_bind_type_t* t = oak_bind_type_in_module(&opts_, module, kind, name);
    return TypeBuilder<T>(*this, t);
  }

  // --- Enum binding ---

  CompileOptions& bind_enum(
      const char* name,
      std::initializer_list<std::pair<const char*, int>> variants)
  {
    return bind_enum_impl(null, name, variants);
  }

  CompileOptions& bind_enum(
      const char* module, const char* name,
      std::initializer_list<std::pair<const char*, int>> variants)
  {
    return bind_enum_impl(module, name, variants);
  }

  // --- Attribute binding ---

  CompileOptions& bind_attr(const oak_bind_attr_t* params)
  {
    oak_bind_attr(&opts_, params);
    return *this;
  }

  oak_compile_options_t* raw() noexcept { return &opts_; }

private:
  template<typename U>
  friend class TypeBuilder;

  // Closures backing C++ bindings; addresses are stable (unique_ptr) and
  // handed to the C side as user_data, so this object must outlive any
  // chunk/VM compiled with it.
  std::vector<std::unique_ptr<detail::native_fn_closure>> fn_closures_;
  std::vector<std::unique_ptr<detail::field_closure>> field_closures_;

  detail::native_fn_closure* store_fn_closure(detail::native_fn_closure c)
  {
    fn_closures_.push_back(
        std::make_unique<detail::native_fn_closure>(std::move(c)));
    return fn_closures_.back().get();
  }

  detail::field_closure* store_field_closure(detail::field_closure c)
  {
    field_closures_.push_back(
        std::make_unique<detail::field_closure>(std::move(c)));
    return field_closures_.back().get();
  }

  CompileOptions& bind_enum_impl(
      const char* module, const char* name,
      std::initializer_list<std::pair<const char*, int>> variants)
  {
    oak_bind_enum_t* e =
        module ? oak_bind_enum_in_module(&opts_, module, name)
               : oak_bind_enum(&opts_, name);
    for (auto& [vname, vval] : variants)
      oak_bind_enum_variant(e, vname, vval);
    return *this;
  }
};

// ===========================================================================
// TypeBuilder<T>
// ===========================================================================

template<typename T>
class TypeBuilder
{
  CompileOptions& opts_;
  oak_bind_type_t* type_;

public:
  TypeBuilder(CompileOptions& opts, oak_bind_type_t* type)
      : opts_(opts), type_(type)
  {
  }

  // Field from member pointer — auto getter/setter
  template<typename M>
  TypeBuilder& field(const char* name, M T::*ptr)
  {
    detail::field_closure fc;
    fc.get = [ptr](oak_value_t self) -> oak_value_t {
      T* instance = static_cast<T*>(oak_native_instance(self));
      return detail::to_oak_value<M>(instance->*ptr);
    };
    fc.set = [ptr](oak_value_t self, oak_value_t val) {
      T* instance = static_cast<T*>(oak_native_instance(self));
      instance->*ptr = detail::from_oak_value<M>(val);
    };

    oak_bind_field_t f{};
    f.name = name;
    f.type = OAK_BIND_SCALAR(detail::oak_type_for<M>);
    f.getter = detail::field_getter_bridge;
    f.setter = detail::field_setter_bridge;
    f.user_data = opts_.store_field_closure(std::move(fc));
    oak_bind_field(type_, &f);
    return *this;
  }

  // Field with explicit getter/setter. `user_data` is forwarded to both.
  TypeBuilder& field(const char* name, oak_bind_type_ref_t type_ref,
                      oak_bind_field_getter_t getter,
                      oak_bind_field_setter_t setter = null,
                      void* user_data = nullptr)
  {
    oak_bind_field_t f{};
    f.name = name;
    f.type = type_ref;
    f.getter = getter;
    f.setter = setter;
    f.user_data = user_data;
    oak_bind_field(type_, &f);
    return *this;
  }

  // Instance method
  template<typename F>
  TypeBuilder& method(const char* name, int arity, F&& fn,
                      oak_bind_type_ref_t return_type =
                          OAK_BIND_SCALAR(OAK_TYPE_VOID))
  {
    oak_bind_fn_t desc{};
    desc.kind = OAK_BIND_FN_INSTANCE_METHOD;
    desc.receiver_type = type_;
    desc.name = name;
    desc.impl = detail::native_fn_bridge;
    desc.user_data =
        opts_.store_fn_closure(detail::adapt_native_fn(std::forward<F>(fn)));
    desc.arity = arity;
    desc.return_type = return_type;
    oak_bind_fn(opts_.raw(), &desc);
    return *this;
  }

  // Static method
  template<typename F>
  TypeBuilder& static_method(const char* name, int arity, F&& fn,
                             oak_bind_type_ref_t return_type =
                                 OAK_BIND_SCALAR(OAK_TYPE_VOID))
  {
    oak_bind_fn_t desc{};
    desc.kind = OAK_BIND_FN_STATIC_METHOD;
    desc.receiver_type = type_;
    desc.name = name;
    desc.impl = detail::native_fn_bridge;
    desc.user_data =
        opts_.store_fn_closure(detail::adapt_native_fn(std::forward<F>(fn)));
    desc.arity = arity;
    desc.return_type = return_type;
    oak_bind_fn(opts_.raw(), &desc);
    return *this;
  }

  // Custom destructor. The runtime invokes it when the native record's
  // refcount hits zero; it must free the instance memory itself.
  TypeBuilder& destructor(oak_bind_destructor_t dtor)
  {
    type_->destructor = dtor;
    return *this;
  }

  // Auto-destructor via delete — only valid for instances allocated with
  // `new T`; pairing it with OAK_ALLOC'd memory is undefined behavior.
  TypeBuilder& destructor()
  {
    type_->destructor = [](void* p) { delete static_cast<T*>(p); };
    return *this;
  }

  oak_bind_type_t* raw() noexcept { return type_; }
};

// ===========================================================================
// VM
// ===========================================================================

class VM
{
  oak_vm_t vm_{};

public:
  explicit VM(Allocator& alloc) { oak_vm_init(&vm_, alloc.get()); }

  ~VM() { oak_vm_free(&vm_); }

  VM(const VM&) = delete;
  VM& operator=(const VM&) = delete;
  VM(VM&&) = delete;
  VM& operator=(VM&&) = delete;

  oak_vm_result_t run(oak_chunk_t* chunk) { return oak_vm_run(&vm_, chunk); }

  oak_vm_result_t run(CompileResult& cr) { return run(cr.chunk()); }

  oak_vm_result_t call(Value fn_val,
                       std::initializer_list<Value> fn_args,
                       Value* out = nullptr)
  {
    constexpr std::size_t max_call_args = 16;
    assert(fn_args.size() <= max_call_args && "too many call arguments");
    if (fn_args.size() > max_call_args)
      return OAK_VM_RUNTIME_ERROR;

    oak_value_t raw_args[max_call_args];
    int argc = 0;
    for (auto& a : fn_args)
      raw_args[argc++] = a.raw();

    oak_value_t raw_out = oak_value_none();
    oak_vm_result_t r =
        oak_vm_call(&vm_, fn_val.raw(), raw_args, argc, &raw_out);

    if (out)
      *out = Value::from_raw(raw_out);
    else
      oak_value_decref(raw_out);

    return r;
  }

  oak_vm_result_t resume() { return oak_vm_resume(&vm_); }

  void set_module_registry(oak_module_registry_t* reg)
  {
    oak_vm_set_module_registry(&vm_, reg);
  }

  void set_module_registry(class ModuleRegistry& reg);

  void set_user_data(void* data) { vm_.user_data = data; }
  void* user_data() const noexcept { return vm_.user_data; }

  oak_vm_t* raw() noexcept { return &vm_; }
};

// ===========================================================================
// ModuleRegistry
// ===========================================================================

class ModuleRegistry
{
  oak_module_registry_t reg_{};

public:
  explicit ModuleRegistry(Allocator& alloc)
  {
    oak_module_registry_init(&reg_, alloc.get());
  }

  ~ModuleRegistry() { oak_module_registry_free(&reg_); }

  ModuleRegistry(const ModuleRegistry&) = delete;
  ModuleRegistry& operator=(const ModuleRegistry&) = delete;
  ModuleRegistry(ModuleRegistry&&) = delete;
  ModuleRegistry& operator=(ModuleRegistry&&) = delete;

  oak_module_t* get(u16 id) const
  {
    return oak_module_registry_get(&reg_, id);
  }

  oak_module_t* find(const char* path) const
  {
    return oak_module_registry_find_by_path(&reg_, path);
  }

  oak_module_registry_t* raw() noexcept { return &reg_; }
};

inline void VM::set_module_registry(ModuleRegistry& reg)
{
  oak_vm_set_module_registry(&vm_, reg.raw());
}

// ===========================================================================
// Free functions
// ===========================================================================

inline ParseResult parse(const char* source, Allocator& alloc,
                         oak_node_kind_t start = OAK_NODE_PROGRAM)
{
  oak_lexer_result_t* lexer = oak_lexer_tokenize(source, alloc.get());
  if (!lexer)
    return {};

  oak_parser_result_t result{};
  oak_parse(lexer, start, &result, alloc.get());
  return ParseResult(lexer, result);
}

inline CompileResult compile(const char* source, CompileOptions& opts)
{
  oak_allocator_t* alloc = opts.raw()->allocator;
  oak_lexer_result_t* lexer = oak_lexer_tokenize(source, alloc);
  if (!lexer)
    return CompileResult{}; // null source or allocation failure; ok() is false

  oak_parser_result_t pr{};
  oak_parse(lexer, OAK_NODE_PROGRAM, &pr, alloc);

  CompileResult cr;
  oak_compile_ex(oak_parser_root(&pr), opts.raw(), cr.raw());

  oak_parser_free(&pr);
  oak_lexer_free(lexer);
  return cr;
}

struct LoadResult
{
  oak_module_loader_result_t result{};

  bool ok() const noexcept { return result.entry != null; }
  oak_module_t* entry() noexcept { return result.entry; }
  int error_count() const noexcept { return result.error_count; }

  Diagnostic error(int i) const { return Diagnostic(&result.errors[i]); }
};

inline LoadResult load_program(const char* path, CompileOptions& opts,
                               ModuleRegistry& reg)
{
  LoadResult lr;
  oak_module_loader_load_program(path, opts.raw(), reg.raw(), &lr.result);
  return lr;
}

} // namespace oak
