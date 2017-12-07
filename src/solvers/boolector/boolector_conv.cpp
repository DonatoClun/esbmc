#include <boolector_conv.h>
#include <cstring>

extern "C" {
#include <btorcore.h>
}

smt_convt *
create_new_boolector_solver(bool int_encoding, const namespacet &ns,
                            const optionst &options,
                            tuple_iface **tuple_api __attribute__((unused)),
                            array_iface **array_api,
                            fp_convt **fp_api __attribute__((unused)))
{
  boolector_convt *conv =
    new boolector_convt(int_encoding, ns, options);
  *array_api = static_cast<array_iface*>(conv);
  *fp_api = static_cast<fp_convt*>(conv);
  return conv;
}

boolector_convt::boolector_convt(bool int_encoding,
                                 const namespacet &ns, const optionst &options)
  : smt_convt(int_encoding, ns), array_iface(false, false), fp_convt(this)
{

  if (int_encoding) {
    std::cerr << "Boolector does not support integer encoding mode"<< std::endl;
    abort();
  }

  btor = boolector_new();
  boolector_set_opt(btor, BTOR_OPT_MODEL_GEN, 1);
  boolector_set_opt(btor, BTOR_OPT_AUTO_CLEANUP, 1);

  if (options.get_option("output") != "") {
    debugfile = fopen(options.get_option("output").c_str(), "w");
  } else {
    debugfile = nullptr;
  }
}

boolector_convt::~boolector_convt()
{
  boolector_delete(btor);

  btor = nullptr;
  if (debugfile)
    fclose(debugfile);
  debugfile = nullptr;
}

smt_convt::resultt
boolector_convt::dec_solve()
{
  pre_solve();

  int result = boolector_sat(btor);

  if (result == BOOLECTOR_SAT)
    return P_SATISFIABLE;

  if (result == BOOLECTOR_UNSAT)
    return P_UNSATISFIABLE;

  return P_ERROR;
}

const std::string
boolector_convt::solver_text()
{
  std::string ss = "Boolector ";
  ss += btor_version(btor);
  return ss;
}

void
boolector_convt::assert_ast(const smt_ast *a)
{
  const btor_smt_ast *ast = btor_ast_downcast(a);
  boolector_assert(btor, ast->e);
#if 0
  if (debugfile)
    boolector_dump_smt(btor, debugfile, ast->e);
#endif
}

smt_ast *
boolector_convt::mk_func_app(const smt_sort *s, smt_func_kind k,
                               const smt_ast * const *args,
                               unsigned int numargs)
{
  const btor_smt_ast *asts[4];
  unsigned int i;

  assert(numargs <= 4);
  for (i = 0; i < numargs; i++) {
    asts[i] = btor_ast_downcast(args[i]);
    // Structs should never reach the SMT solver
    assert(asts[i]->sort->id != SMT_SORT_STRUCT);
  }

  switch (k) {
  case SMT_FUNC_BVADD:
    return new_ast(s, boolector_add(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVSUB:
    return new_ast(s, boolector_sub(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVMUL:
    return new_ast(s, boolector_mul(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVSMOD:
    return new_ast(s, boolector_srem(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVUMOD:
    return new_ast(s, boolector_urem(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVSDIV:
    return new_ast(s, boolector_sdiv(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVUDIV:
    return new_ast(s, boolector_udiv(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVSHL:
    return fix_up_shift(boolector_sll, asts[0], asts[1], s);
  case SMT_FUNC_BVLSHR:
    return fix_up_shift(boolector_srl, asts[0], asts[1], s);
  case SMT_FUNC_BVASHR:
    return fix_up_shift(boolector_sra, asts[0], asts[1], s);
  case SMT_FUNC_BVNEG:
    return new_ast(s, boolector_neg(btor, asts[0]->e));
  case SMT_FUNC_BVNOT:
  case SMT_FUNC_NOT:
    return new_ast(s, boolector_not(btor, asts[0]->e));
  case SMT_FUNC_BVNXOR:
    return new_ast(s, boolector_xnor(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVNOR:
    return new_ast(s, boolector_nor(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVNAND:
    return new_ast(s, boolector_nand(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVXOR:
  case SMT_FUNC_XOR:
    return new_ast(s, boolector_xor(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVOR:
  case SMT_FUNC_OR:
    return new_ast(s, boolector_or(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVAND:
  case SMT_FUNC_AND:
    return new_ast(s, boolector_and(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_IMPLIES:
    return new_ast(s, boolector_implies(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVULT:
    return new_ast(s, boolector_ult(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVSLT:
    return new_ast(s, boolector_slt(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVULTE:
    return new_ast(s, boolector_ulte(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVSLTE:
    return new_ast(s, boolector_slte(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVUGT:
    return new_ast(s, boolector_ugt(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVSGT:
    return new_ast(s, boolector_sgt(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVUGTE:
    return new_ast(s, boolector_ugte(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_BVSGTE:
    return new_ast(s, boolector_sgte(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_EQ:
    return new_ast(s, boolector_eq(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_NOTEQ:
    return new_ast(s, boolector_ne(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_ITE:
    return new_ast(s, boolector_cond(btor, asts[0]->e, asts[1]->e, asts[2]->e));
  case SMT_FUNC_STORE:
    return new_ast(s, boolector_write(btor, asts[0]->e, asts[1]->e,
                                               asts[2]->e));
  case SMT_FUNC_SELECT:
    return new_ast(s, boolector_read(btor, asts[0]->e, asts[1]->e));
  case SMT_FUNC_CONCAT:
    return new_ast(s, boolector_concat(btor, asts[0]->e, asts[1]->e));
  default:
    std::cerr << "Unhandled SMT func \"" << smt_func_name_table[k]
              << "\" in boolector conv" << std::endl;
    abort();
  }
}

smt_sortt
boolector_convt::mk_sort(const smt_sort_kind k, ...)
{
  // Boolector doesn't have any special handling for sorts, they're all always
  // explicit arguments to functions. So, just use the base smt_sort class.
  va_list ap;

  va_start(ap, k);
  switch (k)
  {
    case SMT_SORT_INT:
    case SMT_SORT_REAL:
      std::cerr << "Boolector does not support integer encoding mode"<< std::endl;
      abort();
    case SMT_SORT_FIXEDBV:
    case SMT_SORT_UBV:
    case SMT_SORT_SBV:
    {
      unsigned long uint = va_arg(ap, unsigned long);
      return new boolector_smt_sort(k, boolector_bitvec_sort(btor, uint), uint);
    }
    case SMT_SORT_ARRAY:
    {
      const boolector_smt_sort* dom = va_arg(ap, boolector_smt_sort *); // Consider constness?
      const boolector_smt_sort* range = va_arg(ap, boolector_smt_sort *);

      assert(int_encoding || dom->get_data_width() != 0);

      // The range data width is allowed to be zero, which happens if the range
      // is not a bitvector / integer
      unsigned int data_width = range->get_data_width();
      if (range->id == SMT_SORT_STRUCT || range->id == SMT_SORT_BOOL || range->id == SMT_SORT_UNION)
        data_width = 1;

      return new boolector_smt_sort(k, boolector_array_sort(btor, dom->s, range->s),
          data_width, dom->get_data_width(), range);
    }

    case SMT_SORT_BOOL:
      return new boolector_smt_sort(k, boolector_bool_sort(btor));

    case SMT_SORT_FLOATBV:
    {
      unsigned ew = va_arg(ap, unsigned long);
      unsigned sw = va_arg(ap, unsigned long) + 1;
      return mk_fpbv_sort(ew, sw);
    }

    default:
      break;
  }

  std::cerr << "Unhandled SMT sort in boolector conv" << std::endl;
  abort();
}

smt_ast *
boolector_convt::mk_smt_int(const mp_integer &theint __attribute__((unused)), bool sign __attribute__((unused)))
{
  std::cerr << "Boolector can't create integer sorts" << std::endl;
  abort();
}

smt_ast *
boolector_convt::mk_smt_real(const std::string &str __attribute__((unused)))
{
  std::cerr << "Boolector can't create Real sorts" << std::endl;
  abort();
}

smt_ast *
boolector_convt::mk_smt_bvint(const mp_integer &theint, bool sign, unsigned int width)
{
  smt_sortt s = mk_sort(ctx->int_encoding ? SMT_SORT_INT : sign ? SMT_SORT_SBV : SMT_SORT_UBV, width);

  if (width > 32) {
    // We have to pass things around via means of strings, becausae boolector
    // uses native int types as arguments to its functions, rather than fixed
    // width integers. Seeing how amd64 is LP64, there's no way to pump 64 bit
    // ints to boolector natively.
    if (width > 64) {
      std::cerr <<  "Boolector backend assumes maximum bitwidth is 64, sorry"
                << std::endl;
      abort();
    }

    char buffer[65];
    memset(buffer, 0, sizeof(buffer));

    // Note that boolector has the most significant bit first in bit strings.
    int64_t num = theint.to_int64();
    uint64_t bit = 1ULL << (width - 1);
    for (unsigned int i = 0; i < width; i++) {
      if (num & bit)
        buffer[i] = '1';
      else
        buffer[i] = '0';

      bit >>= 1;
    }

    BoolectorNode *node = boolector_const(btor, buffer);
    return new_ast(s, node);
  }

  BoolectorNode *node;
  if (sign) {
    node = boolector_int(btor, theint.to_long(), boolector_sort_downcast(s)->s);
  } else {
    node =
      boolector_unsigned_int(btor, theint.to_ulong(), boolector_sort_downcast(s)->s);
  }

  return new_ast(s, node);
}

smt_ast *
boolector_convt::mk_smt_bool(bool val)
{
  BoolectorNode *node = (val) ? boolector_true(btor) : boolector_false(btor);
  const smt_sort *sort = boolean_sort;
  return new_ast(sort, node);
}

smt_ast *
boolector_convt::mk_array_symbol(const std::string &name, const smt_sort *s,
                                smt_sortt array_subtype __attribute__((unused)))
{
  return mk_smt_symbol(name, s);
}

smt_ast *
boolector_convt::mk_smt_symbol(const std::string &name, const smt_sort *s)
{
  symtable_type::iterator it = symtable.find(name);
  if (it != symtable.end())
    return it->second;

  BoolectorNode *node;

  switch (s->id)
  {
    case SMT_SORT_SBV:
    case SMT_SORT_UBV:
    case SMT_SORT_FIXEDBV:
      node = boolector_var(btor, boolector_sort_downcast(s)->s, name.c_str());
      break;

    case SMT_SORT_BOOL:
      node = boolector_var(btor, boolector_sort_downcast(s)->s, name.c_str());
      break;

    case SMT_SORT_ARRAY:
      node = boolector_array(btor, boolector_sort_downcast(s)->s, name.c_str());
      break;

    default:
      return nullptr; // Hax.
  }

  btor_smt_ast *ast = new_ast(s, node);

  symtable.insert(symtable_type::value_type(name, ast));
  return ast;
}

smt_sort *
boolector_convt::mk_struct_sort(const type2tc &type __attribute__((unused)))
{
  abort();
}

smt_ast *
boolector_convt::mk_extract(const smt_ast *a, unsigned int high,
                            unsigned int low, const smt_sort *s)
{
  const btor_smt_ast *ast = btor_ast_downcast(a);
  BoolectorNode *b = boolector_slice(btor, ast->e, high, low);
  return new_ast(s, b);
}

expr2tc
boolector_convt::get_bool(const smt_ast *a)
{
  assert(a->sort->id == SMT_SORT_BOOL);
  const btor_smt_ast *ast = btor_ast_downcast(a);
  const char *result = boolector_bv_assignment(btor, ast->e);

  assert(result != NULL && "Boolector returned null bv assignment string");

  expr2tc res;
  switch (*result) {
  case '1':
    res = gen_true_expr();
    break;
  case '0':
    res = gen_false_expr();
    break;
  }

  boolector_free_bv_assignment(btor, result);
  return res;
}

expr2tc
boolector_convt::get_bv(const type2tc &type, smt_astt a)
{
  assert(a->sort->id >= SMT_SORT_SBV || a->sort->id <= SMT_SORT_FIXEDBV);
  const btor_smt_ast *ast = btor_ast_downcast(a);

  const char *result = boolector_bv_assignment(btor, ast->e);
  BigInt val = string2integer(result, 2);
  boolector_free_bv_assignment(btor, result);

  return build_bv(type, val);
}

expr2tc
boolector_convt::get_array_elem(
  const smt_ast *array,
  uint64_t index,
  const type2tc &subtype)
{
  const btor_smt_ast *ast = btor_ast_downcast(array);

  int size;
  char **indicies, **values;
  boolector_array_assignment(btor, ast->e, &indicies, &values, &size);

  BigInt val = 0;
  if(size > 0)
  {
    for (int i = 0; i < size; i++)
    {
      auto idx = string2integer(indicies[i], 2);
      if(idx.to_uint64() == index)
      {
        val = string2integer(values[i], 2);
        break;
      }
    }

    boolector_free_array_assignment(btor, indicies, values, size);
    return build_bv(subtype, val);
  }

  return gen_zero(subtype);
}

const smt_ast *
boolector_convt::overflow_arith(const expr2tc &expr)
{
  const overflow2t &overflow = to_overflow2t(expr);
  const arith_2ops &opers = static_cast<const arith_2ops &>(*overflow.operand);

  const btor_smt_ast *side1 = btor_ast_downcast(convert_ast(opers.side_1));
  const btor_smt_ast *side2 = btor_ast_downcast(convert_ast(opers.side_2));

  // Guess whether we're performing a signed or unsigned comparison.
  bool is_signed = (is_signedbv_type(opers.side_1) ||
                    is_signedbv_type(opers.side_2));

  BoolectorNode *res;
  if (is_add2t(overflow.operand)) {
    if (is_signed) {
      res = boolector_saddo(btor, side1->e, side2->e);
    } else {
      res = boolector_uaddo(btor, side1->e, side2->e);
    }
  } else if (is_sub2t(overflow.operand)) {
    if (is_signed) {
      res = boolector_ssubo(btor, side1->e, side2->e);
    } else {
      res = boolector_usubo(btor, side1->e, side2->e);
    }
  } else if (is_mul2t(overflow.operand)) {
    if (is_signed) {
      res = boolector_smulo(btor, side1->e, side2->e);
    } else {
      res = boolector_umulo(btor, side1->e, side2->e);
    }
  } else if (is_div2t(overflow.operand) || is_modulus2t(overflow.operand)) {
    res = boolector_sdivo(btor, side1->e, side2->e);
  } else {
    return smt_convt::overflow_arith(expr);
  }

  const smt_sort *s = boolean_sort;
  return new_ast(s, res);
}

const smt_ast *
boolector_convt::convert_array_of(smt_astt init_val, unsigned long domain_width)
{
  return default_convert_array_of(init_val, domain_width, this);
}

void
boolector_convt::add_array_constraints_for_solving()
{
}

void
boolector_convt::push_array_ctx()
{
}

void
boolector_convt::pop_array_ctx()
{
}

smt_ast *
boolector_convt::fix_up_shift(shift_func_ptr fptr, const btor_smt_ast *op0,
  const btor_smt_ast *op1, smt_sortt res_sort)
{
  BoolectorNode *data_op, *shift_amount;
  bool need_to_shift_down = false;
  unsigned int bwidth;

  data_op = op0->e;
  bwidth = log2(op0->sort->get_data_width());

  // If we're a non-power-of-x number, some zero extension has to occur
  if (pow(2.0, bwidth) < op0->sort->get_data_width()) {
    // Zero extend up to bwidth + 1
    bwidth++;
    unsigned int new_size = pow(2.0, bwidth);
    unsigned int diff = new_size - op0->sort->get_data_width();
    smt_sortt newsort = mk_sort(SMT_SORT_UBV, new_size);
    smt_astt zeroext = convert_zero_ext(op0, newsort, diff);
    data_op = btor_ast_downcast(zeroext)->e;
    need_to_shift_down = true;
  }

  // We also need to reduce the shift-amount operand down to log2(data_op) len
  shift_amount = boolector_slice(btor, op1->e, bwidth-1, 0);

  BoolectorNode *shift = fptr(btor, data_op, shift_amount);

  // If zero extension occurred, cut off the top few bits of this value.
  if (need_to_shift_down)
    shift = boolector_slice(btor, shift, res_sort->get_data_width() - 1, 0);

  return new_ast(res_sort, shift);
}

const smt_ast* btor_smt_ast::select(smt_convt* ctx, const expr2tc& idx) const
{
  const smt_ast *args[2];
  args[0] = this;
  args[1] = ctx->convert_ast(idx);
  const smt_sort *rangesort = boolector_sort_downcast(sort)->rangesort;
  return ctx->mk_func_app(rangesort, SMT_FUNC_SELECT, args, 2);
}

void boolector_convt::dump_smt()
{
  boolector_dump_smt2(btor, stdout);
}

void btor_smt_ast::dump() const
{
  boolector_dump_smt2_node(boolector_get_btor(e), stdout, e);
}
