#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <type_traits>
#include <algorithm>
#include <memory>
#include <cmath>

static constexpr bool SUPPORT_BINARY_FORMAT = false;// Whether to support %b format type
static constexpr bool STRICT_COMPLIANCE     = true;
static constexpr bool SUPPORT_N_FORMAT      = true; // Whether to support %n format type
static constexpr bool SUPPORT_H_LENGTHS     = true; // Whether to support h and hh length modifiers
static constexpr bool SUPPORT_T_LENGTH      = true; // Whether to support t length modifier
static constexpr bool SUPPORT_J_LENGTH      = true; // Whether to support j length modifier
static constexpr bool SUPPORT_FLOAT_FORMATS = false; // Floating pointing formats
static constexpr bool SUPPORT_A_FORMAT      = false; // Floating point hex format
static constexpr bool SUPPORT_LONG_DOUBLE   = false;
static constexpr bool SUPPORT_POSITIONAL_PARAMETERS = false;

#ifdef __GNUC__
 #define NOINLINE   __attribute__((noinline))
 #define USED_FUNC  __attribute__((used,noinline))
 #pragma GCC push_options
 #pragma GCC optimize ("Ofast")
 #pragma GCC optimize ("no-ipa-cp-clone")
 #pragma GCC optimize ("conserve-stack")
 //#pragma GCC optimize ("no-defer-pop")
 #define likely(x)   __builtin_expect(!!(x), 1)
 #define unlikely(x) __builtin_expect(!!(x), 0)
#else
 #define NOINLINE
 #define USED_FUNC
 #define likely(x)   (x)
 #define unlikely(x) (x)
#endif
#if __cplusplus >= 201400 && (!defined(__GNUC__) || __GNUC__ >= 7)
 #define PASSTHRU   [[fallthrough]];
 #define if_constexpr if constexpr
#else
 #define PASSTHRU
 #define if_constexpr if
#endif
namespace myprintf
{
    typedef std::int_fast64_t  intfmt_t;
    typedef std::uint_fast64_t uintfmt_t;

    static_assert(sizeof(intfmt_t) >= sizeof(long long), "We are unable to print longlong types");
    static_assert(sizeof(intfmt_t) >= sizeof(std::intmax_t), "We are unable to print intmax_t types");
    static_assert(sizeof(std::ptrdiff_t) == sizeof(long long)
               || sizeof(std::ptrdiff_t) == sizeof(long), "We may have problems with %td format");
    static_assert(sizeof(std::size_t) == sizeof(long long)
               || sizeof(std::size_t) == sizeof(long), "We may have problems with %zd format");
    static_assert(sizeof(std::intmax_t) == sizeof(long long)
               || sizeof(std::intmax_t) == sizeof(long), "We may have problems with %jd format");

    // base is one of these:
    static constexpr unsigned char base_decimal   = 10;
    static constexpr unsigned char base_hex       = 16;
    static constexpr unsigned char base_octal     = 8;
    static constexpr unsigned char base_binary    = 2;

    // state.fmt_flags is a bitmask of these:
    // - Flags 0x1F are common to all formats.
    // - Flags 0xFE are interpreted in format_integer().
    // - Flags 0x03 are interpreted in format_string().
    // - Flags 0xFE are interpreted in format_float(), but with different meanings.
    static constexpr unsigned char fmt_leftalign = 0x01; // '-'
    static constexpr unsigned char fmt_zeropad   = 0x02; // '0'
    static constexpr unsigned char fmt_plussign  = 0x04; // '+'
    static constexpr unsigned char fmt_space     = 0x08; // ' '
    static constexpr unsigned char fmt_alt       = 0x10; // '#'
    static constexpr unsigned char fmt_ucbase    = 0x20; // capital hex (%X)
    static constexpr unsigned char fmt_signed    = 0x40; // d,i,p formats
    static constexpr unsigned char fmt_pointer   = 0x80; // p format
    static constexpr unsigned char fmt_exponent  = 0x40;
    static constexpr unsigned char fmt_autofloat = 0x80;

    static constexpr unsigned PatternLength = 8; // number of spaces/zeros in stringconstants

    template<bool SupportFloats> struct GetStringConstants{};
    template<> struct GetStringConstants<false>
    {
        static const char* GetTable() NOINLINE
        {
            static const char stringconstants_intonly[] {
                '-','+',/*' ',*/
                // eight spaces
                ' ',' ',' ',' ', ' ',' ',' ',' ',
                // eight zeros
                '0','0','0','0', '0','0','0','0',
                // table of some multichar prefixes (15 letters)
                /*'0',*/'x',  '0', 'X',
                '(','n','i','l',')', '(','n','u','l','l',')',
                (0),      (2*32+0), (2*32+2), // "", 0x, 0X
                char(5*32+4), char(6*32+9),   // nil,null
            };
            return stringconstants_intonly;
        }
    };
    template<> struct GetStringConstants<true>
    {
        static const char* GetTable() NOINLINE
        {
            static const char stringconstants_floats[] {
                '-','+',/*' ',*/
                // eight spaces
                ' ',' ',' ',' ', ' ',' ',' ',' ',
                // eight zeros
                '0','0','0','0', '0','0','0','0',
                // table of some multichar prefixes (27 letters)
                /*'0',*/'x','n','a','n','i','n','f',
                '0',    'X','N','A','N','I','N','F',
                '(','n','i','l',')', '(','n','u','l','l',')',
                (0),      (2*32+0), (2*32+8),            // "", 0x, 0X
                char(5*32+16), char(6*32+21),            // nil,null
                (3*32+2), (3*32+5), (3*32+10), (3*32+13) // nan,inf,NAN,INF
            };
            return stringconstants_floats;
        }
    };
    static constexpr unsigned char prefix_minus = 1;
    static constexpr unsigned char prefix_plus  = 2;
    static constexpr unsigned char prefix_space = 3;
    static constexpr unsigned char prefix_0x    = 4*1;
    static constexpr unsigned char prefix_0X    = 4*2;
    static constexpr unsigned char prefix_nil   = 4*3;
    static constexpr unsigned char prefix_null  = 4*4;
    static constexpr unsigned char prefix_nan   = 4*5;
    static constexpr unsigned char prefix_inf   = 4*6;
    static constexpr unsigned char prefix_NAN   = 4*7;
    static constexpr unsigned char prefix_INF   = 4*8;
    static constexpr unsigned char prefix_data_length = SUPPORT_FLOAT_FORMATS ? (8+8+5+6) : (2+2+5+6);

    struct prn
    {
        char* param;
        void (*put)(char*,const char*,std::size_t);

        const char* putbegin = nullptr;
        const char* putend   = nullptr;

        char prefixbuffer[SUPPORT_FLOAT_FORMATS ? 4 : 3]; // Longest: +inf or +0x
        char numbuffer[SUPPORT_BINARY_FORMAT ? 64 : 23];
        unsigned char fmt_flags;

        void flush() NOINLINE
        {
            if(likely(putend != putbegin))
            {
                unsigned n = putend-putbegin;
                //std::printf("Flushes %d from <%.*s> to %p\n", n,n,putbegin, param);
                const char* start = putbegin;
                char*      pparam = param;
                // Make sure that the same content will not be printed twice
                putbegin = start  + n;
                param    = pparam + n;
                put(pparam, start, n);
                //std::printf("As a result, %p has <%.*s>\n", param, n, param);
            }
        }
        void append(const char* source, unsigned length) NOINLINE
        {
            //std::printf("Append %d from <%.*s>\n", length, length, source);
            if(likely(length != 0))
            {
                if(source != putend)
                {
                    flush();
                    putbegin = source;
                }
                putend = source+length;
            }
        }
        void append_spaces(const char* from, unsigned count)
        {
            while(count > 0)
            {
                unsigned n = count;
                if(n > PatternLength) n = PatternLength;
                append(from, n);
                count -= n;
            }
        }

        /* sign_mode: 0 = unsigned, 1 = signed, 2 = pointer */
        struct widthinfo
        {
            std::size_t   length;
            unsigned char prefix_index;
        };

        static unsigned clamp(unsigned value, unsigned minvalue, unsigned maxvalue)
        {
            if(value < minvalue) value = minvalue;
            if(value > maxvalue) value = maxvalue;
            return value;
        }
        static unsigned estimate_uinteger_width(uintfmt_t uvalue, unsigned char base) /*NOINLINE*/
        {
            unsigned width = 0;
            while(uvalue != 0)
            {
                ++width;
                uvalue /= base;
            }
            return width;
        }

        static void put_uinteger(char* target, uintfmt_t uvalue, unsigned width, unsigned char base, char alphaoffset) /*NOINLINE*/
        {
            for(unsigned w=width; w-- > 0; )
            {
                unsigned digitvalue = uvalue % base; uvalue /= base;
                target[w] = digitvalue + (likely(digitvalue < 10) ? '0' : alphaoffset);
            }
        }
        static void put_uint_decimal(char* target, uintfmt_t uvalue, unsigned width) NOINLINE
        {
            put_uinteger(target, uvalue, width, 10, '0');
        }

        widthinfo format_integer(intfmt_t value, unsigned char base, unsigned precision)
        {
            // Maximum length is ceil(log8(2^64)) = ceil(64/3+1) = 23 characters (+1 for octal leading zero)
            static_assert(sizeof(numbuffer) >= (SUPPORT_BINARY_FORMAT ? 64 : 23), "Too small numbuffer");

            // Run flush() before we overwrite prefixbuffer/numbuffer,
            // because putbegin/putend can still refer to that data at this point
            flush();

            if(unlikely((fmt_flags & fmt_pointer) && !value)) // (nil) and %p
            {
                return {0, prefix_nil}; // No other prefix
            }

            unsigned char prefix_index = 0;
            if(fmt_flags & fmt_signed) // Pointers and signed values
            {
                if(value < 0)     { value = -value; prefix_index = prefix_minus; }
                else if(fmt_flags & fmt_plussign) { prefix_index = prefix_plus;  }
                else if(fmt_flags & fmt_space)    { prefix_index = prefix_space; }
                // GNU libc printf ignores '+' and ' ' modifiers on unsigned formats, but curiously, not for %p.
                // Note that '+' overrides ' ' if both are used.
            }

            unsigned width = estimate_uinteger_width(value, base);
            if(STRICT_COMPLIANCE && unlikely(fmt_flags & fmt_alt))
            {
                switch(base)
                {
                    case 8:
                        // Make sure there's at least 1 leading '0'
                        if(value != 0 || precision == 0) { width += 1; }
                        break;
                    case 16:
                        // Add 0x/0X prefix
                        if(value != 0) { prefix_index += (fmt_flags & fmt_ucbase) ? prefix_0X : prefix_0x; }
                        break;
                }
            }

            unsigned min_width = 1;
            if(unlikely(precision != ~0u))
            {
                min_width = precision; // 0 is permitted
                // This setting clears out zeropadding according to standard
                if(STRICT_COMPLIANCE) { fmt_flags &= ~fmt_zeropad; }
            }

            // Range check
            width = clamp(width, min_width, sizeof(numbuffer));
            put_uinteger(numbuffer, value, width, base, ((fmt_flags & fmt_ucbase) ? 'A' : 'a')-10);
            return {width, prefix_index};
        }

        template<typename FloatType>
        widthinfo format_float(FloatType value, unsigned char base, unsigned precision)
        {
            unsigned char prefix_index = 0;
            if(value < 0)     { value = -value; prefix_index = prefix_minus; }
            else if(fmt_flags & fmt_plussign) { prefix_index = prefix_plus;  }
            else if(fmt_flags & fmt_space)    { prefix_index = prefix_space; }

            // Run flush() before we overwrite prefixbuffer/numbuffer,
            // because putbegin/putend can still refer to that data at this point
            flush();

            if(!std::isfinite(value))
            {
                return {0, (unsigned char)(prefix_index + (
                                    std::isinf(value) ? ((fmt_flags & fmt_ucbase) ? prefix_INF : prefix_inf)
                                                      : ((fmt_flags & fmt_ucbase) ? prefix_NAN : prefix_nan)))};
            }

            int e_exponent=0;

            if(SUPPORT_A_FORMAT && base == base_hex)
            {
                value = std::frexp(value, &e_exponent);
                while(value > 0 && value*2 < 0x10) { value *= 2; --e_exponent; }
            }
            else if(value != FloatType(0))
            {
                e_exponent = std::floor(std::log10(value));
            }

            if(precision == ~0u) precision = 6;
            if(fmt_flags & fmt_autofloat)
            {
                // Mode: Let X = E-style exponent, P = chosen precision.
                //       If P > X >= -4, choose 'f' and P = P-1-X.
                //       Else,           choose 'e' and P = P-1.
                if(!precision) precision = 1;
                if(int(precision) > e_exponent && e_exponent >= -4) { precision -= e_exponent+1; }
                else                                                { precision -= 1; fmt_flags |= fmt_exponent; }
            }

            int head_width = 1;

            /* Round the value into the specified precision */
            uintfmt_t uvalue = 0;
            if(value != FloatType(0))
            {
                if(!(fmt_flags & fmt_exponent))
                {
                    head_width = 1 + e_exponent;
                }
                unsigned total_precision = precision;
                /*if(head_width > 0)*/ total_precision += head_width;

                // Create a scaling factor where all desired decimals are in the integer portion
                FloatType factor = std::pow(FloatType(10), FloatType(total_precision - std::ceil(std::log10(value))));
                //auto ovalue = value;
                auto rvalue = std::round(value * factor);
                uvalue = rvalue;
                // Scale it back
                value = rvalue / factor;
                // Recalculate exponent from rounded value
                e_exponent = std::floor(std::log10(value));
                if(!(fmt_flags & fmt_exponent))
                {
                    head_width = 1 + e_exponent;
                }

                /*std::printf("Value %.12g rounded to %u decimals: %.12g, %lu  head_width=%d\n",
                    ovalue, total_precision, value, uvalue, head_width);*/
            }

            unsigned exponent_width = 0;
            unsigned point_width    = (precision > 0 || (fmt_flags & fmt_alt)) ? 1 : 0;
            unsigned decimals_width = clamp(precision, 0, sizeof(numbuffer)-exponent_width-point_width-head_width);

            // Count the number of digits
            uintfmt_t digits = estimate_uinteger_width(uvalue, 10);
            uintfmt_t scale    = std::pow(FloatType(10), FloatType(int(digits - head_width)));
            if(!scale) scale=1;
            uintfmt_t head     = uvalue / scale;
            uintfmt_t fraction = uvalue % scale;
            //uintfmt_t scale2   = uintfmt_t(std::pow(FloatType(10), FloatType(-head_width)));
            //if(head_width < 0 && scale2 != 0) fraction /= scale2;

            /*std::printf("- digits in %lu=%lu, scale=%lu, head=%lu, fraction=%lu\n",
                uvalue,digits, scale, head,fraction);*/

            if(head_width < 1) head_width = 1;

            if(fmt_flags & fmt_exponent)
            {
            /*
                auto vv = value * std::pow(FloatType(10), FloatType(-e_exponent));

                fraction = std::modf(vv, &head);
                //if(value != FloatType(0) && head == FloatType(0)) { vv *= 10; fraction = std::modf(vv, &head); --e_exponent; }

                auto uf = fraction;
                fraction = std::round(fraction * std::pow(FloatType(10), FloatType(int(precision))));
                std::printf("%.20g -> %.20g split into head=%.20g, fraction=%.20g -> %.20g using exponents %d\n",
                    value,vv,head,uf,fraction,
                    e_exponent);
            */
                exponent_width =
                    clamp(estimate_uinteger_width(e_exponent<0 ? -e_exponent : e_exponent, 10),
                          2,
                          sizeof(numbuffer)-3-head_width) + 2;
            }
            else
            {
            }

            put_uint_decimal(numbuffer + 0, head, head_width);
            unsigned tgt = head_width;
            if(point_width)
            {
                numbuffer[tgt] = '.'; tgt += 1;
                put_uint_decimal(numbuffer + tgt, fraction, decimals_width);
                tgt += decimals_width;
            }
            if(exponent_width)
            {
                numbuffer[tgt++] = ((fmt_flags & fmt_ucbase) ? 'E' : 'e');
                numbuffer[tgt++] = ((e_exponent < 0)         ? '-' : '+');
                put_uint_decimal(numbuffer + tgt, e_exponent<0 ? -e_exponent : e_exponent, exponent_width-2);
                tgt += exponent_width-2;
            }
            return {tgt, prefix_index};
        }

        void format_string(const char* source, unsigned min_width, unsigned max_width, widthinfo info)
        {
            if(unlikely(source == nullptr)) { /* info.length = 0; */ info.prefix_index = prefix_null; }
            /* There are three possible combinations:
             *
             *    Leftalign      prefix, source, spaces
             *    Zeropad        prefix, zeros,  source
             *    neither        spaces, prefix, source
             *
             * Note that in case of zeropad+leftalign,
             * zeropad is disregarded according to the standard.
             */
            const char* stringconstants = GetStringConstants<SUPPORT_FLOAT_FORMATS>::GetTable();
            unsigned char ctrl = stringconstants[2+PatternLength*2 + prefix_data_length-1 + info.prefix_index/4];
            unsigned prefixlength = ctrl/32;
            const char* prefixsource = &stringconstants[2+PatternLength*2-1 + (ctrl%32)];
            const char* prefix = prefixsource;
            if(info.prefix_index & 3)
            {
                char* tgt = prefixbuffer + sizeof(prefixbuffer) - prefixlength - 1;
                prefix = tgt;
                *tgt = stringconstants[(info.prefix_index&3)-1];
                std::memcpy(tgt+1, prefixsource, prefixlength++);
            }

            // Calculate length of prefix + source
            unsigned sourcelength    = info.length;
            unsigned combined_length = sourcelength + prefixlength;
            // Clamp it into maximum permitted width
            if(combined_length > max_width)
            {
                combined_length = max_width;
                // Figure out how to divide this between prefix and source
                if(unlikely(combined_length <= prefixlength))
                {
                    // Only room to print some of the prefix, and nothing of the source
                    prefixlength = combined_length;
                    sourcelength = 0;
                }
                else
                {
                    // Shorten the source, but print full prefix
                    sourcelength = combined_length - prefixlength;
                }
            }
            // Calculate the padding width
            unsigned pad_flags = min_width > combined_length ? min_width - combined_length : 0;
            pad_flags |= prefixlength << 27; // max length 7 bytes = 3 bits
            // Choose the right mode
            constexpr unsigned flag_prefix_first = 1u << 31;
            constexpr unsigned flag_source_last  = 1u << 30;
            constexpr unsigned noflags           = (1u << 27) - 1;
            // To reduce the register pressure / spilling / compiled binary size,
            // prefixlength and flags are encoded into the same variable that holds
            // the padding width (pad_flags).

            // Note: leftalign overrides zero-padding. Zero-padding also disabled for (nil),(null),nan,inf
            if(fmt_flags & fmt_leftalign)
            {
                pad_flags |= flag_prefix_first;
            }
            else if((fmt_flags & fmt_zeropad) && (!STRICT_COMPLIANCE || likely(info.prefix_index < prefix_nil)))
            {
                pad_flags |= flag_prefix_first | flag_source_last;
                stringconstants += PatternLength; // Zeropad
            }
            else
            {
                pad_flags |= flag_source_last;
            }

            // Only local variables needed here: prefix,source,stringconstants,pad_flags,sourcelength
            if( (pad_flags & flag_prefix_first)) append(prefix, (pad_flags >> 27) & 7);
            if(!(pad_flags & flag_source_last))  append(source, sourcelength);
            append_spaces(stringconstants+2, pad_flags & noflags);
            if(!(pad_flags & flag_prefix_first)) append(prefix, (pad_flags >> 27) & 7);
            if( (pad_flags & flag_source_last))  append(source, sourcelength);
        }
    };

    static unsigned read_int(const char*& fmt, unsigned def) NOINLINE;
    static unsigned read_int(const char*& fmt, unsigned def)
    {
        if(*fmt >= '0' && *fmt <= '9')
        {
            unsigned v = 0;
            do { v = v*10 + (*fmt++ - '0'); } while(*fmt >= '0' && *fmt <= '9');
            return v;
        }
        return def;
    }
    static unsigned read_param_index(const char*& fmt) NOINLINE;
    static unsigned read_param_index(const char*& fmt)
    {
        const char* bkup = fmt;
        unsigned index = read_int(fmt, 0);
        if(*fmt == '$') { ++fmt; return index; }
        fmt = bkup;
        return 0;
    }

    template<bool DoOperation> struct auto_dealloc_pointer {};
    template<> struct auto_dealloc_pointer<false> { typedef unsigned char* type; };
    template<> struct auto_dealloc_pointer<true>  { typedef std::unique_ptr<unsigned char[]> type; };

    /* Note: Compilation of this function depends on the compiler's ability to optimize away
     * code that is never reached because of the state of the constexpr bools.
     * E.g. if SUPPORT_POSITIONAL_PARAMETERS = false, much of the code in this function
     * will end up dummied out and the binary size will be smaller.
     */
    int myvprintf(const char* fmt, std::va_list ap, char* param, void (*put)(char*,const char*,std::size_t)) NOINLINE;
    int myvprintf(const char* fmt_begin, std::va_list ap, char* param, void (*put)(char*,const char*,std::size_t))
    {
        prn state;
        state.param = param;
        state.put   = put;

        /* Positional parameters support:
         * Pass 3: Calculate the number of parameters,
                   then allocate array of sizes
                   Or, if no positional parameters were found,
                   jump straight to step 0
         * Pass 2: Populate the array of sizes,
         *         then convert it into array of offsets,
         *         and allocate array of data,
         *         and populate array of data
         * Pass 1: Actually print,
         *         then free the two arrays, and exit
         * Pass 0: Actually print (no pos. params)
         */
        //printf("---Interpret %s\n", fmt_begin);

        constexpr unsigned MAX_AUTO_PARAMS = 0x8000, MAX_ROUNDS = 4, POS_PARAM_MUL = MAX_AUTO_PARAMS * MAX_ROUNDS;
        auto_dealloc_pointer<SUPPORT_POSITIONAL_PARAMETERS>::type param_data_table{};

        // Figure out the largest parameter size. This is a compile-time constant.
        constexpr std::size_t largest = std::max(std::max(sizeof(long long), sizeof(void*)),
                                                 SUPPORT_FLOAT_FORMATS ? std::max(sizeof(double),
                                                   SUPPORT_LONG_DOUBLE ? sizeof(long double) : sizeof(long))
                                                                       : sizeof(long));
        // "Round" variable encodes, starting from lsb:
        //     - log2(MAX_AUTO_PARAMS) bits: number of auto params counted so far
        //     - 2 bits:                     round number
        //     - The rest:                   maximum explicit param index found so far
        for(unsigned round = SUPPORT_POSITIONAL_PARAMETERS ? (3*MAX_AUTO_PARAMS) : 0; ; )
        {
            auto process_param = [&round,&param_data_table](unsigned typecode, unsigned which_param_index, auto type)
            {
                if(which_param_index == 0)
                {
                    which_param_index = round++ % MAX_AUTO_PARAMS;
                }
                else
                {
                    if(which_param_index > round / POS_PARAM_MUL)
                        round = round % POS_PARAM_MUL + which_param_index * POS_PARAM_MUL;
                    --which_param_index;
                }
                unsigned short* param_offset_table = reinterpret_cast<unsigned short *>(&param_data_table[0]);
                switch((round / MAX_AUTO_PARAMS) % MAX_ROUNDS)
                {
                    case 2:
                        param_offset_table[which_param_index] = typecode;
                        break;
                    case 1:
                        return *(std::remove_reference_t<decltype(type)> const*)
                                  &param_data_table[largest * param_offset_table[which_param_index]];
                }
                return type;
            };

            // Rounds 0 and 1 are action rounds. Rounds 2 and 3 are not (nothing is printed).
            const bool action_round = !SUPPORT_POSITIONAL_PARAMETERS || !(round & (MAX_AUTO_PARAMS*2));

            // Start parsing the format string from beginning
            const char* fmt = fmt_begin;
            for(; likely(*fmt != '\0'); ++fmt)
            {
                if(likely(*fmt != '%'))
                {
                literal:
                    if(action_round) state.append(fmt, 1);
                    continue;
                }
                if(unlikely(*++fmt == '%')) { goto literal; }

                #define GET_ARG(acquire_type, variable, type_index, which_param_index) \
                    acquire_type variable = (SUPPORT_POSITIONAL_PARAMETERS && round != 0) \
                        ? process_param((/*sizeof(acquire_type)*8 + */type_index), which_param_index, decltype(variable){}) \
                        : (/*std::printf("va_arg(%s)\n", #acquire_type),*/ va_arg(ap, acquire_type))

                // Read possible position-index for the value (it comes before flags / widths)
                unsigned param_index = SUPPORT_POSITIONAL_PARAMETERS ? read_param_index(fmt) : 0;

                // Read format flags
                unsigned char fmt_flags = 0;
            moreflags:;
                switch(*fmt)
                {
                    case '-': fmt_flags |= fmt_leftalign; moreflags1: ++fmt; goto moreflags;
                    case ' ': fmt_flags |= fmt_space;     goto moreflags1;
                    case '+': fmt_flags |= fmt_plussign;  goto moreflags1;
                    case '#': fmt_flags |= fmt_alt;       goto moreflags1;
                    case '0': fmt_flags |= fmt_zeropad;   goto moreflags1;
                }

                // Read possible min-width
                unsigned min_width = 0;
                if(*fmt == '*')
                {
                    ++fmt;

                    unsigned opt_index = SUPPORT_POSITIONAL_PARAMETERS ? read_param_index(fmt) : 0;
                    GET_ARG(int,v,0, opt_index);

                    min_width = (v < 0) ? -v : v;
                    if(v < 0) { fmt_flags |= fmt_leftalign; } // negative value sets left-aligning
                }
                else
                {
                    min_width = read_int(fmt, min_width);
                }

                // Read possible precision / max-width
                unsigned precision = ~0u;
                if(*fmt == '.')
                {
                    if(*++fmt == '*')
                    {
                        ++fmt;

                        unsigned opt_index = SUPPORT_POSITIONAL_PARAMETERS ? read_param_index(fmt) : 0;
                        GET_ARG(int,v,0, opt_index);

                        if(v >= 0) { precision = v; } // negative value is treated as unset
                    }
                    else
                    {
                        precision = read_int(fmt, precision);
                    }
                }

                // Read possible length modifier.
                // The numeric base is encoded into the same variable to reduce the number
                // of local variables, thereby reducing register pressure, stack usage,
                // spilling etc., resulting in a smaller compiled binary.
                constexpr unsigned BASE_MUL = 0x10;
                unsigned size_base_spec = (base_decimal-1) + BASE_MUL*sizeof(int);
                switch(*fmt)
                {
                    case 't': if(!SUPPORT_T_LENGTH) break;
                              size_base_spec = (base_decimal-1) + BASE_MUL*sizeof(std::ptrdiff_t); ++fmt; break;
                    case 'z': size_base_spec = (base_decimal-1) + BASE_MUL*sizeof(std::size_t);    ++fmt; break;
                    case 'l': size_base_spec = (base_decimal-1) + BASE_MUL*sizeof(long);       if(*++fmt != 'l') break; PASSTHRU
                    case 'L': size_base_spec = (base_decimal-1) + BASE_MUL*sizeof(long long);      ++fmt; break; // Or 'long double'
                    case 'j': if(!SUPPORT_J_LENGTH) break;
                              size_base_spec = (base_decimal-1) + BASE_MUL*sizeof(std::intmax_t);  ++fmt; break;
                    case 'h': if(!SUPPORT_H_LENGTHS) break;
                              size_base_spec = (base_decimal-1) + BASE_MUL*sizeof(short);      if(*++fmt != 'h') break; /*PASSTHRU*/
                              size_base_spec = (base_decimal-1) + BASE_MUL*sizeof(char);           ++fmt; break;
                }

                // Read the format type
                const char* source = state.numbuffer;
                prn::widthinfo info{0,0};
                state.fmt_flags = fmt_flags;
                switch(*fmt)
                {
                    case '\0': goto unexpected;
                    case 'n':
                    {
                        if(!SUPPORT_N_FORMAT) goto got_int;
                        GET_ARG(void*,pointer,3, param_index);
                        if(!action_round) continue;

                        auto value = state.param - param;
                        if(unlikely((size_base_spec/BASE_MUL) != sizeof(int)))
                        {
                            if(sizeof(long) != sizeof(long long)
                            && (size_base_spec/BASE_MUL) == sizeof(long long))  { *static_cast<long long*>(pointer) = value; }
                            else if(sizeof(int) != sizeof(long)
                                 && (size_base_spec/BASE_MUL) == sizeof(long))  { *static_cast<long*>(pointer) = value; }
                            else if(SUPPORT_H_LENGTHS
                                 && sizeof(int) != sizeof(short)
                                 && (size_base_spec/BASE_MUL) == sizeof(short)) { *static_cast<short*>(pointer) = value; }
                            else /*if(SUPPORT_H_LENGTHS
                                 && sizeof(int) != sizeof(char)
                                 && (size_base_spec/BASE_MUL) == sizeof(char))*/{ *static_cast<signed char*>(pointer) = value; }
                        }
                        else                                  { *static_cast<int*>(pointer) = value; }
                        continue; // Nothing to format
                    }
                    case 's':
                    {
                        GET_ARG(void*,pointer,3, param_index);
                        if(!action_round) continue;

                        source = static_cast<const char*>(pointer);
                        if(source)
                        {
                            info.length = std::strlen(source);
                            // Only calculate length on non-null pointers
                        }
                        // precision is treated as maximum width
                        break;
                    }
                    case 'c':
                    {
                        GET_ARG(int,c,0, param_index);
                        if(!action_round) continue;

                        state.numbuffer[0] = static_cast<char>(c);
                        info.length = 1;
                        if(STRICT_COMPLIANCE)
                        {
                            precision = ~0u; // No max-width
                        }
                        break;
                    }
                    case 'p': { state.fmt_flags |= fmt_alt | fmt_pointer | fmt_signed; size_base_spec = BASE_MUL*sizeof(void*); } PASSTHRU
                    case 'x': {                                size_base_spec = (size_base_spec & ~(BASE_MUL-1)) + (base_hex-1);   goto got_int; }
                    case 'X': { state.fmt_flags |= fmt_ucbase; size_base_spec = (size_base_spec & ~(BASE_MUL-1)) + (base_hex-1);   goto got_int; }
                    case 'o': {                                size_base_spec = (size_base_spec & ~(BASE_MUL-1)) + (base_octal-1); goto got_int; }
                    case 'b': { if(SUPPORT_BINARY_FORMAT) { size_base_spec = (size_base_spec & ~(BASE_MUL-1)) + (base_binary-1); } goto got_int; }
                    case 'd': /*PASSTHRU*/
                    case 'i': { state.fmt_flags |= fmt_signed; goto got_int; }
                    case 'u': default: got_int:
                    {
                        intfmt_t value = 0;

                        if(sizeof(long) != sizeof(long long)
                        && (size_base_spec/BASE_MUL) == sizeof(long long))  { GET_ARG(long long,v,2, param_index); value = v; }
                        else if(sizeof(int) != sizeof(long)
                             && (size_base_spec/BASE_MUL) == sizeof(long))  { GET_ARG(long,v,1, param_index); value = v; }
                        else
                        {
                            GET_ARG(int,v,0, param_index);
                            value = v;
                            if(SUPPORT_H_LENGTHS && (size_base_spec/BASE_MUL) != sizeof(int))
                            {
                                if(sizeof(int) != sizeof(short) && (size_base_spec/BASE_MUL) == sizeof(short)) { value = (signed short)value; }
                                else /*if(sizeof(int) != sizeof(char) && (size_base_spec/BASE_MUL) == sizeof(char))*/ { value = (signed char)value; }
                            }
                        }
                        if(!action_round) continue;
                        if(!(state.fmt_flags & fmt_signed) && (size_base_spec/BASE_MUL) < sizeof(uintfmt_t))
                        {
                            value &= ((uintfmt_t(1) << (8*(size_base_spec/BASE_MUL)))-1);
                        }

                        info = state.format_integer(value, size_base_spec%BASE_MUL+1, precision);
                        precision = ~0u; // No max-width
                        break;
                    }

                    case 'A': if(!SUPPORT_FLOAT_FORMATS || !SUPPORT_A_FORMAT) goto got_int; state.fmt_flags |= fmt_ucbase; PASSTHRU
                    case 'a': if(!SUPPORT_FLOAT_FORMATS || !SUPPORT_A_FORMAT) goto got_int; size_base_spec = (size_base_spec & ~(BASE_MUL-1)) + (base_hex-1);
                              if(precision == ~0u) { } /* TODO: set enough precision for exact representation */
                              goto got_fmt_e;
                    case 'E': if(!SUPPORT_FLOAT_FORMATS) goto got_int; state.fmt_flags |= fmt_ucbase; PASSTHRU
                    case 'e': if(!SUPPORT_FLOAT_FORMATS) goto got_int;
                              // Set up 'e' flags
                    got_fmt_e:state.fmt_flags |= fmt_exponent; // Mode: Always exponent
                              goto got_flt;
                    case 'G': if(!SUPPORT_FLOAT_FORMATS) goto got_int; state.fmt_flags |= fmt_ucbase; PASSTHRU
                    case 'g': if(!SUPPORT_FLOAT_FORMATS) goto got_int;
                              // Set up 'g' flags
                              state.fmt_flags |= fmt_autofloat; // Mode: Autodetect
                              goto got_flt;
                    case 'F': if(!SUPPORT_FLOAT_FORMATS) goto got_int; state.fmt_flags |= fmt_ucbase; PASSTHRU
                    case 'f': if(!SUPPORT_FLOAT_FORMATS) goto got_int; got_flt:
                    {
                        if(precision == ~0u) precision = 6;
                        if(SUPPORT_LONG_DOUBLE && (size_base_spec/BASE_MUL) == sizeof(long long))
                        {
                            GET_ARG(long double,value,5, param_index);
                            if(!action_round) continue;
                            info = state.format_float(value, size_base_spec%BASE_MUL+1, precision);
                        }
                        else
                        {
                            GET_ARG(double,value,4, param_index);
                            if(!action_round) continue;
                            info = state.format_float(value, size_base_spec%BASE_MUL+1, precision);
                        }
                        precision = ~0u; // No max-width
                        break;
                    }
                    /* f,F: [-]ddd.ddd
                     *                     Recognize [-]inf and nan (INF/NAN for 'F')
                     *      Precision = Number of decimals after decimal point (assumed 6)
                     *
                     * e,E: [-]d.ddde+dd
                     *                     Exactly one digit before decimal point
                     *                     At least two digits in exponent
                     *
                     * g,G: Like e, if exponent is < -4 or >= precision
                     *      Otherwise like f, but
                     *
                     * a,A: [-]0xh.hhhhp+d  Exactly one hex-digit before decimal point
                     *                      Number of digits after it = precision.
                     */
                }
                if(action_round) // This condition is redundant, but seems to reduce binary size
                state.format_string(source, min_width, precision, info);
                #undef GET_ARG
            }
        unexpected:;
            // Format string processing is complete.
            if_constexpr(!SUPPORT_POSITIONAL_PARAMETERS)
            {
                goto exit_rounds;
            }
            else
            {
                // Do book-keeping after each round. See notes in the beginning of this function.
                unsigned n_params = std::max(round % MAX_AUTO_PARAMS, round / POS_PARAM_MUL);
                unsigned rndno = (round / MAX_AUTO_PARAMS) % MAX_ROUNDS;
                unsigned paramdata_units  = n_params;
                unsigned paramsize_units = (n_params * sizeof(unsigned short) + largest-1) / largest;
                switch(rndno)
                {
                    case 3:
                    {
                        if(round/POS_PARAM_MUL == 0)
                        {
                            // No positional parameters, jump to round 0
                            round = 0;
                            continue;
                        }
                        // Allocate room for offsets and parameters in one go.
                        //printf("%u params, sizesize=%u datasize=%u largest=%zu\n", n_params, paramsize_size, paramdata_size, largest);
                        param_data_table = auto_dealloc_pointer<SUPPORT_POSITIONAL_PARAMETERS>::type(
                            new unsigned char[largest * (paramsize_units + paramdata_units)]);
                        // It is likely we allocated too much (for example if all parameters are ints),
                        // but this way we only need one allocation for the entire duration of the printf.
                        break;
                    }
                    case 2:
                    {
                        unsigned short* param_offset_table = reinterpret_cast<unsigned short *>(&param_data_table[0]);
                        for(unsigned n=0; n<n_params; ++n)
                        {
                            // Convert the size & typecode into an offset
                            unsigned /*size = param_offset_table[n]/8,*/ typecode = param_offset_table[n]/*%8*/;
                            unsigned offset = n + paramsize_units;
                            param_offset_table[n] = offset;
                            // Load the parameter and store it
                            unsigned char* tgt = &param_data_table[largest * offset];
                            switch(typecode)
                            {
                                default:{ type0:; int v = va_arg(ap,int);     std::memcpy(tgt,&v,sizeof(v)); } break;
                                case 1: { long      v = va_arg(ap,long);      std::memcpy(tgt,&v,sizeof(v)); } break;
                                case 2: { long long v = va_arg(ap,long long); std::memcpy(tgt,&v,sizeof(v)); } break;
                                case 3: { void*     v = va_arg(ap,void*);     std::memcpy(tgt,&v,sizeof(v)); } break;
                                case 4: if(!SUPPORT_FLOAT_FORMATS) goto type0;
                                        { double      v = va_arg(ap,double);      std::memcpy(tgt,&v,sizeof(v)); } break;
                                case 5: if(!SUPPORT_FLOAT_FORMATS || !SUPPORT_LONG_DOUBLE) goto type0;
                                        { long double v = va_arg(ap,long double); std::memcpy(tgt,&v,sizeof(v)); } break;
                            }
                        }
                        break;
                    }
                    default:
                        goto exit_rounds;
                }
                round = (rndno-1) * MAX_AUTO_PARAMS;
            }
        }
    exit_rounds:;
        state.flush();
        return state.param - param;
    }
}
#ifdef __GNUC__
 #pragma GCC pop_options
#endif


extern "C" {
    static void wfunc(char*, const char* src, std::size_t n)
    {
        /* PUT HERE YOUR CONSOLE-PRINTING FUNCTION */
        extern int _write(int fd, const unsigned char* buffer, unsigned num, unsigned mode=0);
        _write(1, (const unsigned char*) src, n);
    }

    int __wrap_printf(const char* fmt, ...) USED_FUNC;
    int __wrap_printf(const char* fmt, ...)
    {
        std::va_list ap;
        va_start(ap, fmt);
        int ret = myprintf::myvprintf(fmt, ap, nullptr, wfunc);
        va_end(ap);
        return ret;
    }

    int __wrap_vprintf(const char* fmt, std::va_list ap) USED_FUNC;
    int __wrap_vprintf(const char* fmt, std::va_list ap)
    {
        return myprintf::myvprintf(fmt, ap, nullptr, wfunc);
    }

    int __wrap_vfprintf(std::FILE*, const char* fmt, std::va_list ap) USED_FUNC;
    int __wrap_vfprintf(std::FILE*, const char* fmt, std::va_list ap)
    {
        return myprintf::myvprintf(fmt, ap, nullptr, wfunc);
    }

    //int __wrap_fiprintf(std::FILE*, const char* fmt, ...) USED_FUNC;
    int __wrap_fiprintf(std::FILE*, const char* fmt, ...)
    {
        std::va_list ap;
        va_start(ap, fmt);
        int ret = myprintf::myvprintf(fmt, ap, nullptr, wfunc);
        va_end(ap);
        return ret;
    }

    int __wrap_fprintf(std::FILE*, const char* fmt, ...) USED_FUNC;
    int __wrap_fprintf(std::FILE*, const char* fmt, ...)
    {
        std::va_list ap;
        va_start(ap, fmt);
        int ret = myprintf::myvprintf(fmt, ap, nullptr, wfunc);
        va_end(ap);
        return ret;
    }

    /*static void mfunc(char* target, const char* src, std::size_t n)
    {
        //std::printf("mfunc(%p,%p,%zu)\n", target,src,n);
        //for(std::size_t a=0; a<n; ++a) target[a] = src[a];
        std::memcpy(target, src, n);
    }*/

    int __wrap_vsprintf(char* target, const char* fmt, std::va_list ap) USED_FUNC;
    int __wrap_vsprintf(char* target, const char* fmt, std::va_list ap)
    {
        typedef void (*afunc)(char*,const char*,std::size_t);

        //int ret = myprintf::myvprintf(fmt, ap, target, mfunc);
        int ret = myprintf::myvprintf(fmt, ap, target, (afunc)std::memcpy);

        //std::printf("target = %d = <%.*s>\n", ret, ret, target);
        target[ret] = '\0';
        return ret;
    }

    int __wrap_sprintf(char* target, const char* fmt, ...) USED_FUNC;
    int __wrap_sprintf(char* target, const char* fmt, ...)
    {
        std::va_list ap;
        va_start(ap, fmt);
        int ret = __wrap_vsprintf(target, fmt, ap);
        va_end(ap);
        return ret;
    }

    //int __wrap_fflush(std::FILE*) USED_FUNC;
    int __wrap_fflush(std::FILE*)
    {
        return 0;
    }

    int __wrap_puts(const char* str) USED_FUNC;
    int __wrap_puts(const char* str)
    {
        return __wrap_printf("%s\r\n", str); // %s\r\n
    }

    int __wrap_fputs(std::FILE*, const char* str) USED_FUNC;
    int __wrap_fputs(std::FILE*, const char* str)
    {
        return __wrap_puts(str);
    }

    int __wrap_putchar(int c) USED_FUNC;
    int __wrap_putchar(int c)
    {
        char ch = c;
        wfunc(nullptr, &ch, 1);
        return c;
    }

    int __wrap_fwrite(void* buffer, std::size_t a, std::size_t b, std::FILE*) USED_FUNC;
    int __wrap_fwrite(void* buffer, std::size_t a, std::size_t b, std::FILE*)
    {
        wfunc(nullptr, (const char*)buffer, a*b);
        return a*b;
    }
}
