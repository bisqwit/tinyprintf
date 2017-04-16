#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <cmath>

static constexpr bool SUPPORT_BINARY_FORMAT = false;// Whether to support %b format type
static constexpr bool STRICT_COMPLIANCE     = true;
static constexpr bool SUPPORT_N_FORMAT      = false;// Whether to support %n format type
static constexpr bool SUPPORT_H_LENGTHS     = true; // Whether to support h and hh length modifiers
static constexpr bool SUPPORT_T_LENGTH      = true; // Whether to support t length modifier
static constexpr bool SUPPORT_J_LENGTH      = true; // Whether to support j length modifier
static constexpr bool SUPPORT_FLOAT_FORMATS = false; // Floating pointing formats
static constexpr bool SUPPORT_A_FORMAT      = false; // Floating point hex format
static constexpr bool SUPPORT_LONG_DOUBLE   = false;

#ifdef __GNUC__
 #define NOINLINE   __attribute__((noinline))
 #define USED_FUNC  __attribute__((used,noinline))
 #pragma GCC push_options
 #pragma GCC optimize ("Ofast")
 #pragma GCC optimize ("no-ipa-cp-clone")
 #define likely(x)   __builtin_expect(!!(x), 1)
 #define unlikely(x) __builtin_expect(!!(x), 0)
#else
 #define NOINLINE
 #define USED_FUNC
 #define likely(x)   (x)
 #define unlikely(x) (x)
#endif
#if __cplusplus >= 201400 && (!defined(__GNUC__) || __GNUC__ >= 7)
# define PASSTHRU   [[fallthrough]];
#else
 #define PASSTHRU
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
    static const char stringconstants[] {
        '-','+',/*' ',*/
        // eight spaces
        ' ',' ',' ',' ', ' ',' ',' ',' ',
        // eight zeros
        '0','0','0','0', '0','0','0','0',
        // table of some multichar prefixes (27 letters)
        /*'0',*/'x','n','a','n','i','n','f',
        '0',    'X','N','A','N','I','N','F',
        '(','n','i','l',')', '(','n','u','l','l',')',
        (0),                            // none
        (2*32+0), (3*32+2),  (3*32+5),  // 0x,nan,inf
        (2*32+8), (3*32+10), (3*32+13), // 0X,NAN,INF
        char(5*32+16), char(6*32+21),
    };
    static constexpr unsigned char prefix_minus = 1;
    static constexpr unsigned char prefix_plus  = 2;
    static constexpr unsigned char prefix_space = 3;
    static constexpr unsigned char prefix_0x    = 4*1;
    static constexpr unsigned char prefix_nan   = 4*2;
    static constexpr unsigned char prefix_inf   = 4*3;
    static constexpr unsigned char prefix_0X    = 4*4;
    static constexpr unsigned char prefix_NAN   = 4*5;
    static constexpr unsigned char prefix_INF   = 4*6;
    static constexpr unsigned char prefix_nil   = 4*7;
    static constexpr unsigned char prefix_null  = 4*8;

    struct prn
    {
        char* param;
        void (*put)(char*,const char*,std::size_t);

        const char* putbegin = nullptr;
        const char* putend   = nullptr;

        char numbuffer[SUPPORT_BINARY_FORMAT ? 64 : 22];
        unsigned char fmt_flags;

        void flush() NOINLINE
        {
            if(likely(putend != putbegin))
            {
                unsigned n = putend-putbegin;
                //std::printf("Flushes %d from <%.*s> to %p\n", n,n,putbegin, param);
                put(param, putbegin, n);
                //std::printf("As a result, %p has <%.*s>\n", param, n, param);
                param += n;
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
            unsigned width=0;
            while(uvalue != 0)
            {
                ++width;
                uvalue /= base;
            }
            return width;
        }
        void put_uinteger(char* target, uintfmt_t uvalue, unsigned width, unsigned char base) /*NOINLINE*/
        {
            char lett = ((fmt_flags & fmt_ucbase) ? 'A' : 'a')-10;
            for(unsigned w=width; w-- > 0; )
            {
                unsigned digitvalue = uvalue % base; uvalue /= base;
                target[w] = digitvalue + (digitvalue < 10 ? '0' : lett);
            }
        }

        widthinfo format_integer(intfmt_t value, unsigned char base, unsigned precision)
        {
            // Maximum length is ceil(log8(2^64)) = ceil(64/3) = 22 characters
            static_assert(sizeof(numbuffer) >= (SUPPORT_BINARY_FORMAT ? 64 : 22), "Too small numbuffer");

            if(unlikely((fmt_flags & fmt_pointer) && !value)) // (nil) and %p
            {
                return {0, prefix_nil}; // No other prefix
            }

            unsigned char prefix_index = 0;
            if(fmt_flags & fmt_signed) // Pointers and signed values
            {
                bool negative = value < 0;
                if(negative)      { value = -value; prefix_index = prefix_minus; }
                else if(fmt_flags & fmt_plussign) { prefix_index = prefix_plus;  }
                else if(fmt_flags & fmt_space)    { prefix_index = prefix_space; }
                // GNU libc printf ignores '+' and ' ' modifiers on unsigned formats, but curiously, not for %p.
                // Note that '+' overrides ' ' if both are used.
            }

            unsigned min_width = 1;
            if(unlikely(precision != ~0u))
            {
                min_width = precision; // 0 is permitted
                // This setting clears out zeropadding according to standard
                if(STRICT_COMPLIANCE) { fmt_flags &= ~fmt_zeropad; }
            }

            unsigned width = estimate_uinteger_width(value, base);
            if(unlikely(fmt_flags & fmt_alt) && likely(value != 0))
            {
                switch(base)
                {
                    case 8: { width += 1; break; } // Add '0' prefix
                    case 16: { prefix_index += (fmt_flags & fmt_ucbase) ? prefix_0X : prefix_0x; break; } // Add 0x/0X prefix
                }
            }

            // Range check
            width = clamp(width, min_width, sizeof(numbuffer));
            put_uinteger(numbuffer, value, width, base);
            return {width, prefix_index};
        }

        template<typename FloatType>
        widthinfo format_float(FloatType value, unsigned char base, unsigned precision)
        {
            unsigned char prefix_index = 0;
            bool negative = value < 0;
            if(negative)      { value = -value; prefix_index = prefix_minus; }
            else if(fmt_flags & fmt_plussign) { prefix_index = prefix_plus;  }
            else if(fmt_flags & fmt_space)    { prefix_index = prefix_space; }

            if(!std::isfinite(value))
            {
                return {0, (unsigned char)(prefix_index | (
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

            put_uinteger(numbuffer + 0, head, head_width, 10);
            unsigned tgt = head_width;
            if(point_width)
            {
                numbuffer[tgt] = '.'; tgt += 1;
                put_uinteger(numbuffer + tgt, fraction, decimals_width, 10);
                tgt += decimals_width;
            }
            if(exponent_width)
            {
                numbuffer[tgt++] = ((fmt_flags & fmt_ucbase) ? 'E' : 'e');
                numbuffer[tgt++] = ((e_exponent < 0)         ? '-' : '+');
                put_uinteger(numbuffer + tgt, e_exponent<0 ? -e_exponent : e_exponent, exponent_width-2, 10);
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
            char prefixbuffer[4]; // Longest: +inf
            unsigned char ctrl = stringconstants[2+PatternLength*2 + 26 + info.prefix_index/4], prefixlen = ctrl/32;
            const char* prefixsource = &stringconstants[2+PatternLength*2-1 + (ctrl%32)];
            const char* prefix = prefixsource;
            if(info.prefix_index & 3)
            {
                prefixbuffer[0] = stringconstants[(info.prefix_index&3)-1];
                std::memcpy(prefixbuffer+1, prefixsource, prefixlen++);
                prefix = prefixbuffer;
            }

            // Calculate length of prefix + source
            unsigned combined_length = info.length + prefixlen;
            // Clamp it into maximum permitted width
            if(combined_length > max_width)
            {
                combined_length = max_width;
                // Figure out how to divide this between prefix and source
                if(unlikely(combined_length <= prefixlen))
                {
                    // Only room to print some of the prefix, and nothing of the source
                    prefixlen   = combined_length;
                    info.length = 0;
                }
                else
                {
                    // Shorten the source, but print full prefix
                    info.length = combined_length - prefixlen;
                }
            }
            // Calculate the padding width
            unsigned pad = min_width > combined_length ? (min_width - combined_length) : 0;

            // Choose the right mode
            bool zeropad = (fmt_flags & fmt_zeropad) && (!STRICT_COMPLIANCE || likely(info.prefix_index < prefix_nil)); // Disable zero-padding for (nil), (null)
            bool prefix_first = (fmt_flags & fmt_leftalign) || zeropad;
            bool source_last  = !(fmt_flags & fmt_leftalign);

            if(prefix_first)  append(prefix, prefixlen);
            if(!source_last)  append(source, info.length);
            append_spaces((zeropad && (!STRICT_COMPLIANCE || !(fmt_flags & fmt_leftalign)))
                            ? (stringconstants+2+PatternLength) : (stringconstants+2), pad);
            if(!prefix_first) append(prefix, prefixlen);
            if(source_last)   append(source, info.length);
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

    int myvprintf(const char* fmt, std::va_list ap, char* param, void (*put)(char*,const char*,std::size_t)) NOINLINE;
    int myvprintf(const char* fmt, std::va_list ap, char* param, void (*put)(char*,const char*,std::size_t))
    {
        prn state;
        state.param = param;
        state.put   = put;

        for(; likely(*fmt != '\0'); ++fmt)
        {
            if(likely(*fmt != '%'))
            {
            literal:
                state.append(fmt, 1);
                continue;
            }
            if(unlikely(*++fmt == '%')) { goto literal; }

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

            #define GET_ARG(acquire_type) va_arg(ap, acquire_type)

            unsigned min_width = 0;
            if(*fmt == '*')
            {
                ++fmt;
                int v = GET_ARG(int);
                min_width = (v < 0) ? -v : v;
                if(v < 0) { fmt_flags |= fmt_leftalign; } // negative value sets left-aligning
            }
            else
            {
                min_width = read_int(fmt, min_width);
            }

            unsigned precision = ~0u;
            if(*fmt == '.')
            {
                if(*++fmt == '*')
                {
                    ++fmt;
                    int v = GET_ARG(int);
                    if(v >= 0) { precision = v; } // negative value is treated as unset
                }
                else
                {
                    precision = read_int(fmt, precision);
                }
            }

            unsigned char intsize = sizeof(int);
            unsigned char base    = base_decimal;
            switch(*fmt)
            {
                case 't': if(!SUPPORT_T_LENGTH) break;
                          intsize = sizeof(std::ptrdiff_t); ++fmt; break;
                case 'z': intsize = sizeof(std::size_t);    ++fmt; break;
                case 'l': intsize = sizeof(long);       if(*++fmt != 'l') break; PASSTHRU
                case 'L': intsize = sizeof(long long);      ++fmt; break; // Or 'long double'
                case 'j': if(!SUPPORT_J_LENGTH) break;
                          intsize = sizeof(std::intmax_t);  ++fmt; break;
                case 'h': if(!SUPPORT_H_LENGTHS) break;
                          intsize = sizeof(short);      if(*++fmt != 'h') break; /*PASSTHRU*/
                          intsize = sizeof(char);           ++fmt; break;
            }
            state.fmt_flags = fmt_flags;

            char* source = state.numbuffer;
            prn::widthinfo info{0,0};

            switch(*fmt)
            {
                case '\0': goto unexpected;
                case 'n':
                {
                    if(!SUPPORT_N_FORMAT) goto got_int;
                    auto value = state.param - param;
                    void* pointer = GET_ARG(void*);
                    if(unlikely(intsize != sizeof(int)))
                    {
                        if(sizeof(long) != sizeof(long long)
                        && intsize == sizeof(long long))  { *static_cast<long long*>(pointer) = value; }
                        else if(sizeof(int) != sizeof(long)
                             && intsize == sizeof(long))  { *static_cast<long*>(pointer) = value; }
                        else if(SUPPORT_H_LENGTHS
                             && sizeof(int) != sizeof(short)
                             && intsize == sizeof(short)) { *static_cast<short*>(pointer) = value; }
                        else /*if(SUPPORT_H_LENGTHS
                             && sizeof(int) != sizeof(char)
                             && intsize == sizeof(char))*/{ *static_cast<signed char*>(pointer) = value; }
                    }
                    else                                  { *static_cast<int*>(pointer) = value; }
                    continue; // Nothing to format
                }
                case 's':
                {
                    source = static_cast<char*>( GET_ARG(void*) );
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
                    source[0] = static_cast<char>( GET_ARG(int) );
                    info.length = 1;
                    if(STRICT_COMPLIANCE)
                    {
                        precision = ~0u; // No max-width
                    }
                    break;
                }
                case 'p': { state.fmt_flags |= fmt_alt | fmt_pointer | fmt_signed; intsize = sizeof(void*); } PASSTHRU
                case 'x': {                                base = base_hex;   goto got_int; }
                case 'X': { state.fmt_flags |= fmt_ucbase; base = base_hex;   goto got_int; }
                case 'o': {                                base = base_octal; goto got_int; }
                case 'b': { if(SUPPORT_BINARY_FORMAT) { base = base_binary; } goto got_int; }
                case 'd': /*PASSTHRU*/
                case 'i': { state.fmt_flags |= fmt_signed; goto got_int; }
                case 'u': default: got_int:
                {
                    intfmt_t value = 0;

                    if(sizeof(long) != sizeof(long long)
                    && intsize == sizeof(long long))  { value = GET_ARG(long long); }
                    else if(sizeof(int) != sizeof(long)
                         && intsize == sizeof(long))  { value = GET_ARG(long); }
                    else
                    {
                        value = GET_ARG(int);
                        if(SUPPORT_H_LENGTHS && intsize != sizeof(int))
                        {
                            if(sizeof(int) != sizeof(short) && intsize == sizeof(short)) { value = (signed short)value; }
                            else /*if(sizeof(int) != sizeof(char) && intsize == sizeof(char))*/ { value = (signed char)value; }
                        }
                    }
                    if(!(state.fmt_flags & fmt_signed) && intsize < sizeof(uintfmt_t))
                    {
                        value &= ((uintfmt_t(1) << (8*intsize))-1);
                    }

                    info = state.format_integer(value, base, precision);
                    precision = ~0u; // No max-width
                    break;
                }

                case 'A': if(!SUPPORT_FLOAT_FORMATS || !SUPPORT_A_FORMAT) goto got_int; state.fmt_flags |= fmt_ucbase; PASSTHRU
                case 'a': if(!SUPPORT_FLOAT_FORMATS || !SUPPORT_A_FORMAT) goto got_int; base = base_hex;
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
                    if(SUPPORT_LONG_DOUBLE && intsize == sizeof(long long))
                    {
                        long double value = GET_ARG(long double);
                        info = state.format_float(value, base, precision);
                    }
                    else
                    {
                        double value = GET_ARG(double);
                        info = state.format_float(value, base, precision);
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
            state.format_string(source, min_width, precision, info);
            #undef GET_ARG
        }
    unexpected:;
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
