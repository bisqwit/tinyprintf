#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdint>

static constexpr bool SUPPORT_BINARY_FORMAT = false;

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

    struct argument
    {
        unsigned short min_width=0, max_width=65535;
        enum basetype : unsigned char { decimal=10, hex=16,  hexup=16+64,  oct=8, bin=2 } base = decimal;

        unsigned intsize:3;
        bool leftalign:1, zeropad:1, sign:1, space:1, alt:1;

        static_assert(sizeof(long long)-1 <= 7, "intsize bitfield is too small");

        argument() : min_width(0), max_width(65535),
                     intsize(sizeof(int)-1), leftalign(false), zeropad(false), sign(false), space(false), alt(false)
        {
        }
    };

    static const char spacebuffer[8+8+3 + 4+3+3+6+7] {
        // eight spaces
        ' ',' ',' ',' ', ' ',' ',' ',' ',
        // eight zeros
        '0','0','0','0', '0','0','0','0',
        // first three prefixes
        '-','+',' ',
        // last three prefixes
        4,7,10,16,
        2,'0','x', 2,'0','X', 5,'(','n','i','l',')', 6,'(','n','u','l','l',')'
    };

    static constexpr unsigned char prefix_minus = 0x1;
    static constexpr unsigned char prefix_plus  = 0x2;
    static constexpr unsigned char prefix_space = 0x3;
    static constexpr unsigned char prefix_0x    = 0x4;
    static constexpr unsigned char prefix_0X    = 0x8;
    static constexpr unsigned char prefix_nil   = 0xC;
    static constexpr unsigned char prefix_null  = 0x10;

    struct prn
    {
        char* param;
        void (*put)(char*,const char*,std::size_t);

        const char* putbegin = nullptr;
        const char* putend   = nullptr;

        argument arg;

        unsigned char prefix_index = 0;
        char numbuffer[SUPPORT_BINARY_FORMAT ? 65 : 23];

        void flush() NOINLINE
        {
            if(likely(putend != putbegin))
            {
                unsigned n = putend-putbegin;
                put(param, putbegin, n);
                param += n;
            }
        }
        void append(const char* source, unsigned length) NOINLINE
        {
            if(source != putend)
            {
                flush();
                putbegin = source;
            }
            putend = source+length;
        }
        unsigned format_integer(intfmt_t value, bool uns, char mode)
        {
            // Maximum length is ceil(log8(2^64)) + 1 sign character = ceil(64/3+1) = 23 characters
            static_assert(sizeof(numbuffer) >= (SUPPORT_BINARY_FORMAT ? 65 : 23), "Too small numbuffer");

            if(!uns || unlikely(mode == 'p'))
            {
                bool negative = value < 0;
                if(negative)       { value = -value; prefix_index |= prefix_minus; }
                else if(arg.sign)  {                 prefix_index |= prefix_plus;  }
                else if(arg.space) {                 prefix_index |= prefix_space; }
                // GNU libc printf ignores '+' and ' ' modifiers on unsigned formats,
                // but curiously, not for %p
            }

            uintfmt_t uvalue = value;
            unsigned base = arg.base & 63;
            char     lett = ((arg.base & 64) ? 'A' : 'a')-10;

            unsigned width = 0;
            for(uintfmt_t uvalue_test = uvalue; ; )
            {
                ++width;
                uvalue_test /= base;
                if(uvalue_test == 0) break;
            }
            // width is at least 1 now.
            if(unlikely(arg.alt) && likely(uvalue != 0))
            {
                switch(base)
                {
                    case 8:  width += 1; break; // Add '0' prefix
                    case 16: prefix_index |= (arg.base & 64) ? prefix_0X : prefix_0x; break; // Add 0x/0X prefix
                }
            }
            else if(unlikely(!uvalue && mode=='p'))
            {
                prefix_index = prefix_nil; // Discards other prefixes
                return 0;
            }

            // For integers, the length limit (.xx) has a different meaning:
            // Minimum number of digits printed.
            if(unlikely(arg.max_width != 65535))
            {
                if(arg.max_width > width) width = arg.max_width; // width can only grow here.
                arg.max_width = 65535;
            }
            // Range check
            if(unlikely(width > sizeof(numbuffer))) width = sizeof(numbuffer);

            char* target = numbuffer + width;
            unsigned length = target - numbuffer;
            do {
                unsigned digitvalue = uvalue % base; uvalue /= base;
                *--target = digitvalue + (digitvalue < 10 ? '0' : lett);
            } while(--width); // width has a starting value of at least 1.
            return length;
        }
        void append_spaces(const char* from, unsigned number) NOINLINE
        {
            while(number > 0)
            {
                unsigned n = number;
                if(n > 8) n = 8;
                append(from, n);
                number -= n;
            }
        }
        void format_string(const char* source, unsigned length) NOINLINE
        {
            /* There are three possible combinations:
             *
             *    Leftalign Zeropad      Print
             *           no      no      spacebuffer, prefix, source
             *          yes      no      prefix, source, spacebuffer
             *          ---     yes      prefix, spacebuffer, source
             */
            char prefixbuffer[6], *prefixend = prefixbuffer; // Max length: "+(nil)" or "(null)" = 6 chars
            if(unlikely(!source)) { length = 0; prefix_index = prefix_null; }
            if(prefix_index & 0x3) *prefixend++ = spacebuffer[16 + (prefix_index&0x3)-1];
            if(prefix_index & 0x1C)
            {
                const char* src = spacebuffer+16+3+spacebuffer[16+3+(prefix_index&0x1C)/4-1];
                std::memcpy(prefixend, src+1, *src);
                prefixend += *src;
                // Disable zero-padding for (nil), (null)
                if(unlikely(prefix_index >= 0xC)) arg.zeropad = false;
            }
            unsigned prefixlen = prefixend - prefixbuffer;

            // Calculate length of prefix + source
            unsigned combined_length = length + prefixlen;
            // Clamp it into maximum permitted width
            if(combined_length > arg.max_width)
            {
                combined_length = arg.max_width;
                // Figure out how to divide this between prefix and source
                if(combined_length <= prefixlen)
                {
                    // Only room to print some of the prefix, and nothing of the source
                    prefixlen = combined_length;
                    length    = 0;
                }
                else
                {
                    // Shorten the source, but print full prefix
                    length    = combined_length - prefixlen;
                }
            }
            // Calculate the padding width
            unsigned pad = arg.min_width > combined_length ? (arg.min_width - combined_length) : 0;

            // Choose the right mode
            if(arg.zeropad)
            {
                // Prefix, then spacebuffer, then source
                append(prefixbuffer, prefixlen);
                append_spaces(spacebuffer+8, pad);
                append(source, length);
            }
            else if(arg.leftalign)
            {
                // Prefix, then source, then spacebuffer
                append(prefixbuffer, prefixlen);
                append(source, length);
                append_spaces(spacebuffer, pad);
            }
            else
            {
                // Spacebuffer, then prefix, then source
                append_spaces(spacebuffer, pad);
                append(prefixbuffer, prefixlen);
                append(source, length);
            }
        }
    };

    int myvprintf(const char* fmt, std::va_list ap, char* param, void (*put)(char*,const char*,std::size_t)) NOINLINE;
    int myvprintf(const char* fmt, std::va_list ap, char* param, void (*put)(char*,const char*,std::size_t))
    {
        prn state;
        state.param = param;
        state.put   = put;

        while(likely(*fmt != '\0'))
        {
            if(likely(*fmt != '%'))
            {
            literal:
                state.append(fmt++, 1);
                continue;
            }
            if(*++fmt == '%') { goto literal; }

            state.arg = argument{};
        moreflags:;
            switch(*fmt)
            {
                case '-': state.arg.leftalign = true; ++fmt; goto moreflags;
                case ' ': state.arg.space     = true; ++fmt; goto moreflags;
                case '+': state.arg.sign      = true; ++fmt; goto moreflags;
                case '#': state.arg.alt       = true; ++fmt; goto moreflags;
                case '0': state.arg.zeropad   = true; ++fmt; goto moreflags;
            }
            if(*fmt == '*') { int v = va_arg(ap, int); if(v < 0) { state.arg.leftalign = true; v = -v; } state.arg.min_width = v; ++fmt; }
            else while(*fmt >= '0' && *fmt <= '9') { state.arg.min_width = state.arg.min_width*10 + (*fmt++ - '0'); }

            if(*fmt == '.')
            {
                ++fmt;
                if(*fmt == '*') { int v = va_arg(ap, int); if(v >= 0) { state.arg.max_width = v; } ++fmt; }
                else if(*fmt >= '0' && *fmt <= '9') { state.arg.max_width = 0; do {
                   state.arg.max_width = state.arg.max_width*10 + (*fmt++ - '0'); } while(*fmt >= '0' && *fmt <= '9'); }
            }
            switch(*fmt)
            {
                case 't': state.arg.intsize = sizeof(std::ptrdiff_t)-1; ++fmt; break;
                case 'z': state.arg.intsize = sizeof(std::size_t)-1;    ++fmt; break;
                case 'l': state.arg.intsize = sizeof(long)-1;       if(*++fmt != 'l') break; PASSTHRU
                case 'L': state.arg.intsize = sizeof(long long)-1;      ++fmt; break;
                case 'j': state.arg.intsize = sizeof(std::intmax_t)-1;  ++fmt; break;
                case 'h': state.arg.intsize = sizeof(short)-1;      if(*++fmt != 'h') break; /*PASSTHRU*/
                          state.arg.intsize = sizeof(char)-1;           ++fmt; break;
            }
            switch(*fmt)
            {
                case '\0': goto unexpected;
                case 'n':
                {
                    auto value = state.param - param;
                    if(sizeof(long) != sizeof(long long)
                    && state.arg.intsize == sizeof(long long)-1) { *va_arg(ap, long long*) = value; }
                    else if(sizeof(int) != sizeof(long)
                         && state.arg.intsize == sizeof(long)-1) { *va_arg(ap, long*) = value; }
                    else if(sizeof(int) != sizeof(short)
                         && state.arg.intsize == sizeof(short)-1) { *va_arg(ap, short*) = value; }
                    else if(sizeof(int) != sizeof(char)
                         && state.arg.intsize == sizeof(char)-1) { *va_arg(ap, signed char*) = value; }
                    else                                         { *va_arg(ap, int*) = value; }
                    break;
                }
                case 's':
                {
                    const char* str = va_arg(ap, const char*);
                    state.format_string(str, str ? std::strlen(str) : 0);
                    break;
                }
                case 'c':
                {
                    state.numbuffer[0] = va_arg(ap, int);
                    state.format_string(state.numbuffer, 1);
                    break;
                }
                case 'p': { state.arg.alt = true; state.arg.intsize = sizeof(void*)-1; } PASSTHRU
                case 'x': { state.arg.base = argument::hex;   goto got_int; }
                case 'X': { state.arg.base = argument::hexup; goto got_int; }
                case 'o': { state.arg.base = argument::oct;   goto got_int; }
                case 'b': if(SUPPORT_BINARY_FORMAT) { state.arg.base = argument::bin;   goto got_int; } else { goto got_int; }
                default:
                {
                got_int:;
                    intfmt_t value = 0;
                    bool     uns   = state.arg.base != argument::decimal || *fmt == 'u';

                    if(sizeof(long) != sizeof(long long)
                    && state.arg.intsize == sizeof(long long)-1)  { value = va_arg(ap, long long); }
                    else if(sizeof(int) != sizeof(long)
                         && state.arg.intsize == sizeof(long)-1)  { value = va_arg(ap, long); }
                    else if(sizeof(int) != sizeof(short)
                         && state.arg.intsize == sizeof(short)-1) { value = (signed short)va_arg(ap, int); }
                    else if(sizeof(int) != sizeof(char)
                         && state.arg.intsize == sizeof(char)-1)  { value = (signed char)va_arg(ap, int); }
                    else                                          { value = va_arg(ap, int); }
                    if(uns && state.arg.intsize < sizeof(uintfmt_t)-1) { value &= ((uintfmt_t(1) << (8*(state.arg.intsize+1)))-1); }

                    state.format_string(state.numbuffer, state.format_integer(value, uns, *fmt));
                    break;
                }
            }
            ++fmt;
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

    int __wrap_vsprintf(char* target, const char* fmt, std::va_list ap) USED_FUNC;
    int __wrap_vsprintf(char* target, const char* fmt, std::va_list ap)
    {
        typedef void (*afunc_t)(char*,const char*,std::size_t);

        int ret = myprintf::myvprintf(fmt, ap, target, (afunc_t) std::memcpy);
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
        /*
        unsigned len = std::strlen(str);
        wfunc(nullptr, str, len);
        wfunc(nullptr, myprintf::spacebuffer+17, 2); // \r\n
        return len+2;
        */
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
