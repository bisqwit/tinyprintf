#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdint>

static constexpr bool SUPPORT_BINARY_FORMAT = false;// Whether to support b format type
static constexpr bool STRICT_COMPLIANCE     = true;
static constexpr bool SUPPORT_H_FORMATS     = true; // Whether to support h and hh length modifiers
static constexpr bool SUPPORT_T_FORMAT      = true; // Whether to support t length modifier
static constexpr bool SUPPORT_J_FORMAT      = true; // Whether to support j length modifier

#ifdef __GNUC__
 #define NOINLINE   __attribute__((noinline))
 #define USED_FUNC  __attribute__((used,noinline))
 #pragma GCC push_options
 #pragma GCC optimize ("Ofast")
 #define likely(x)   __builtin_expect(!!(x), 1)
 #define unlikely(x) __builtin_expect(!!(x), 0)
#else
 #define NOINLINE
 #define USED_FUNC
 #define likely(x)   (x)
 #define unlikely(x) (x)
#endif
#if __cplusplus >= 201400
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
    // - Flags 0xE0 are only used in integer formats.
    static constexpr unsigned char fmt_leftalign = 0x01; // '-'
    static constexpr unsigned char fmt_zeropad   = 0x02; // '0'
    static constexpr unsigned char fmt_plussign  = 0x04; // '+'
    static constexpr unsigned char fmt_space     = 0x08; // ' '
    static constexpr unsigned char fmt_alt       = 0x10; // '#'
    static constexpr unsigned char fmt_ucbase    = 0x20; // capital hex (%X)
    static constexpr unsigned char fmt_signed    = 0x40; // d,i,p formats
    static constexpr unsigned char fmt_pointer   = 0x80; // p format

    static constexpr unsigned PatternLength = 8; // number of spaces/zeros in stringconstants
    static const char stringconstants[] {
        // eight spaces
        ' ',' ',' ',' ', ' ',' ',' ',' ',
        // eight zeros
        '0','0','0','0', '0','0','0','0',
        // table of beginnings and lengths for all different prefixes (length: 14)
        0,       1*32+0,  1*32+3, 1*32+6,   // "-+ "
        2*32+1,  3*32+0,  3*32+3, 3*32+6,   // "-+ " with "0x"
        2*32+15, 3*32+14, 3*32+17, 3*32+20, // "-+ " with "0X"
        char(5*32+9), char(6*32+23), // (nil) and (null)
        // table of multichar prefixes (3*3+5+3*3+6 = 29 characters
        '-','0','x','+','0','x',' ','0','x','(','n','i','l',')',
        '-','0','X','+','0','X',' ','0','X','(','n','u','l','l',')'
    };
    static constexpr unsigned char prefix_minus = 1;
    static constexpr unsigned char prefix_plus  = 2;
    static constexpr unsigned char prefix_space = 3;
    static constexpr unsigned char prefix_0x    = 4; // 4..7
    static constexpr unsigned char prefix_0X    = 8; // 8..11
    static constexpr unsigned char prefix_nil   = 12;
    static constexpr unsigned char prefix_null  = 13;
    static constexpr unsigned num_prefixes = 14;

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

        /* sign_mode: 0 = unsigned, 1 = signed, 2 = pointer */
        struct widthinfo
        {
            std::size_t   length;
            unsigned char prefix_index;
        };
        widthinfo format_integer(intfmt_t value, unsigned char base, unsigned max_width)
        {
            // Maximum length is ceil(log8(2^64)) = ceil(64/3) = 22 characters
            static_assert(sizeof(numbuffer) >= (SUPPORT_BINARY_FORMAT ? 64 : 22), "Too small numbuffer");

            if(unlikely((fmt_flags & fmt_pointer) && !value)) // (nil) and %p
            {
                return {0, prefix_nil}; // Discards other prefixes
            }

            unsigned char prefix_index = 0;
            if(fmt_flags & fmt_signed) // Pointers and signed values
            {
                bool negative = value < 0;
                if(negative)   { value = -value; prefix_index = prefix_minus; }
                else if(fmt_flags & fmt_plussign) { prefix_index = prefix_plus;  }
                else if(fmt_flags & fmt_space)    { prefix_index = prefix_space; }
                // GNU libc printf ignores '+' and ' ' modifiers on unsigned formats, but curiously, not for %p.
                // Note that '+' overrides ' ' if both are used.
            }

            uintfmt_t uvalue = value;
            char     lett = ((fmt_flags & fmt_ucbase) ? 'A' : 'a')-10;

            unsigned width = 0;
            for(uintfmt_t uvalue_test = uvalue; uvalue_test != 0 || (!STRICT_COMPLIANCE && width==0); )
            {
                ++width;
                uvalue_test /= base;
            }
            if(unlikely(fmt_flags & fmt_alt) && likely(uvalue != 0))
            {
                switch(base)
                {
                    case 8:  width += 1; break; // Add '0' prefix
                    case 16: prefix_index += (fmt_flags & fmt_ucbase) ? prefix_0X : prefix_0x; break; // Add 0x/0X prefix
                }
            }
            // For integers, the length limit (.xx) has a different meaning:
            // Minimum number of digits printed.
            if(unlikely(max_width != ~0u))
            {
                if(max_width > width) width = max_width; // width can only grow here.
                // This setting clears out zeropadding according to standard
                if(STRICT_COMPLIANCE) { fmt_flags &= ~fmt_zeropad; }
            }
            else if(STRICT_COMPLIANCE && width == 0)
            {
                // Zero-width is permitted if explicitly specified,
                // but otherwise we always print at least 1 digit.
                width = 1;
            }

            // Range check
            if(unlikely(width > sizeof(numbuffer))) width = sizeof(numbuffer);

            for(unsigned w=width; w-- > 0; )
            {
                unsigned digitvalue = uvalue % base; uvalue /= base;
                numbuffer[w] = digitvalue + (digitvalue < 10 ? '0' : lett);
            }
            return {width, prefix_index};
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
        void format_string(const char* source, unsigned min_width, unsigned max_width, widthinfo info)
        {
            if(unlikely(source == nullptr)) { /* info.length = 0; */ info.prefix_index = prefix_null; }
            /* There are three possible combinations:
             *
             *    Leftalign      prefix, source, spaces
             *    Zeropad        prefix, spaces, source
             *    neither        spaces, prefix, source
             *
             * Note that in case of zeropad+leftalign,
             * zeropad is disregarded according to the standard.
             */
            const unsigned char ctrl = stringconstants[PatternLength*2 + info.prefix_index];
            const char* const prefix = &stringconstants[PatternLength*2 + num_prefixes] + (ctrl%32);
            unsigned prefixlen = ctrl/32;

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
            bool zeropad = (fmt_flags & fmt_zeropad) && (!STRICT_COMPLIANCE || likely(info.prefix_index < 12)); // Disable zero-padding for (nil), (null)
            bool prefix_first = (fmt_flags & fmt_leftalign) || zeropad;
            bool source_last  = !(fmt_flags & fmt_leftalign);

            if(prefix_first)  append(prefix, prefixlen);
            if(!source_last)  append(source, info.length);
            append_spaces((zeropad && (!STRICT_COMPLIANCE || !(fmt_flags & fmt_leftalign)))
                            ? (stringconstants+PatternLength) : (stringconstants), pad);
            if(!prefix_first) append(prefix, prefixlen);
            if(source_last)   append(source, info.length);
        }
    };

    static void read_int(const char*& fmt, unsigned& value) NOINLINE;
    static void read_int(const char*& fmt, unsigned& value)
    {
        if(*fmt >= '0' && *fmt <= '9')
        {
            unsigned v = 0;
            do { v = v*10 + (*fmt++ - '0'); } while(*fmt >= '0' && *fmt <= '9');
            value = v;
        }
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
                read_int(fmt, min_width);
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
                    read_int(fmt, precision);
                }
            }

            unsigned char intsize = sizeof(int);
            unsigned char base    = base_decimal;
            switch(*fmt)
            {
                case 't': if(!SUPPORT_T_FORMAT) break;
                          intsize = sizeof(std::ptrdiff_t); ++fmt; break;
                case 'z': intsize = sizeof(std::size_t);    ++fmt; break;
                case 'l': intsize = sizeof(long);       if(*++fmt != 'l') break; PASSTHRU
                case 'L': intsize = sizeof(long long);      ++fmt; break;
                case 'j': if(!SUPPORT_J_FORMAT) break;
                          intsize = sizeof(std::intmax_t);  ++fmt; break;
                case 'h': if(!SUPPORT_H_FORMATS) break;
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
                    auto value = state.param - param;
                    void* pointer = GET_ARG(void*);
                    if(sizeof(long) != sizeof(long long)
                    && intsize == sizeof(long long))  { *static_cast<long long*>(pointer) = value; }
                    else if(sizeof(int) != sizeof(long)
                         && intsize == sizeof(long))  { *static_cast<long*>(pointer) = value; }
                    else if(SUPPORT_H_FORMATS
                         && sizeof(int) != sizeof(short)
                         && intsize == sizeof(short)) { *static_cast<short*>(pointer) = value; }
                    else if(SUPPORT_H_FORMATS
                         && sizeof(int) != sizeof(char)
                         && intsize == sizeof(char))  { *static_cast<signed char*>(pointer) = value; }
                    else                              { *static_cast<int*>(pointer) = value; }
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
                case 'u': got_int:
                {
                    {intfmt_t value = 0;

                    if(sizeof(long) != sizeof(long long)
                    && intsize == sizeof(long long))  { value = GET_ARG(long long); }
                    else if(sizeof(int) != sizeof(long)
                         && intsize == sizeof(long))  { value = GET_ARG(long); }
                    else
                    {
                        value = GET_ARG(int);
                        if(SUPPORT_H_FORMATS)
                        {
                            if(sizeof(int) != sizeof(short) && intsize == sizeof(short)) { value = (signed short)value; }
                            else if(sizeof(int) != sizeof(char) && intsize == sizeof(char)) { value = (signed char)value; }
                        }
                    }
                    if(!(state.fmt_flags & fmt_signed) && intsize < sizeof(uintfmt_t))
                    {
                        value &= ((uintfmt_t(1) << (8*intsize))-1);
                    }

                    info = state.format_integer(value, base, precision);}
                    precision = ~0u; // No max-width
                    break;
                }
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
