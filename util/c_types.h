/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_C_TYPES_H
#define CPROVER_C_TYPES_H

#include <irep2.h>
#include <expr.h>

typet index_type();
type2tc index_type2();
typet enum_type();
typet int_type();
type2tc int_type2();
typet uint_type();
type2tc uint_type2();
typet signed_short_int_type();
typet unsigned_short_int_type();
typet long_int_type();
typet long_long_int_type();
typet long_uint_type();
typet long_long_uint_type();
typet char_type();
typet unsigned_char_type();
typet signed_char_type();
typet char16_type();
typet char32_type();
typet wchar_type();
type2tc char_type2();
typet float_type();
typet double_type();
typet long_double_type();
typet size_type();
typet signed_size_type();
typet bool_type();

typet pointer_type();
type2tc pointer_type2();

#endif
