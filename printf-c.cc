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
#else
 #define NOINLINE
 #define USED_FUNC
#endif
namespace myprintf
{
    struct argument
    {
        unsigned short min_width=0, max_width=65535;
        enum basetype : unsigned char { decimal=10, hex=16,  hexup=16+64,  oct=8, bin=2 } base = decimal;

        unsigned longs:2;
        bool leftalign:1, zeropad:1, sign:1;

        argument() : min_width(0), max_width(65535),
                     longs(0), leftalign(false), zeropad(false), sign(false)
        {
        }
    };

    static const char spacebuffer[8+8+1] = {
        ' ',' ',' ',' ', ' ',' ',' ',' ',
        '0','0','0','0', '0','0','0','0', 'x' /*, '\r','\n'*/};

    struct prn
    {
        char* param;
        void (*put)(char*,const char*,std::size_t);

        const char* putbegin = nullptr;
        const char* putend   = nullptr;

        char numbuffer[SUPPORT_BINARY_FORMAT ? 66 : 24];
        argument arg;

        void flush() NOINLINE
        {
            if(putend != putbegin)
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
        unsigned format_integer(std::int_fast64_t value, bool uns)
        {
            // Maximum length is ceil(log8(2^64)) + 1 characters + nul = ceil(64/3+2) = 24 characters
            static_assert(sizeof(numbuffer) >= (SUPPORT_BINARY_FORMAT ? 66 : 24), "Too small numbuffer");

            char* target = numbuffer;
            bool negative = value < 0 && !uns;
            if(negative)      { *target++ = '-'; value = -value; }
            else if(arg.sign) { *target++ = '+'; }

            std::uint_fast64_t uvalue = value;
            unsigned base = arg.base & 63;
            char     lett = ((arg.base & 64) ? 'A' : 'a')-10;

            unsigned width = 0;
            for(std::uint_fast64_t uvalue_test = uvalue; ; )
            {
                ++width;
                uvalue_test /= base;
                if(uvalue_test == 0) break;
            }
            // width is at least 1.

            // For integers, the length limit (.xx) has a different meaning:
            // Minimum number of digits printed.
            if(arg.max_width < sizeof(numbuffer))
            {
                if(width < arg.max_width) { width = arg.max_width; } // width can only grow here.
                arg.max_width = 65535;
            }

            target += width;
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
            if(length > arg.max_width) length = arg.max_width;
            unsigned remain = 0;
            if(length < arg.min_width)
            {
                unsigned pad = arg.min_width - length;
                if(arg.leftalign)
                    { remain = pad; }
                else
                    { append_spaces(spacebuffer + (arg.zeropad ? 8 : 0), pad); }
            }
            append(source, length);
            append_spaces(spacebuffer, remain);
        }
    };

    int myvprintf(const char* fmt, std::va_list ap, char* param, void (*put)(char*,const char*,std::size_t)) NOINLINE;
    int myvprintf(const char* fmt, std::va_list ap, char* param, void (*put)(char*,const char*,std::size_t))
    {
        prn state;
        state.param = param;
        state.put   = put;

        while(*fmt)
        {
            if(*fmt != '%')
            {
            literal:
                state.append(fmt++, 1);
                continue;
            }
            if(*++fmt == '%') { goto literal; }

            state.arg = argument{};
            if(*fmt == '-') { state.arg.leftalign = true; ++fmt; }
            if(*fmt == '+') { state.arg.sign      = true; ++fmt; }
            if(*fmt == '0') { state.arg.zeropad   = true; ++fmt; }
            if(*fmt == '*') { int v = va_arg(ap, int); if(v < 0) { state.arg.leftalign = true; v = -v; } state.arg.min_width = v; ++fmt; }
            else while(*fmt >= '0' && *fmt <= '9') { state.arg.min_width = state.arg.min_width*10 + (*fmt++ - '0'); }

            if(*fmt == '.')
            {
                ++fmt;
                state.arg.max_width = 0;
                if(*fmt == '*') { state.arg.max_width = va_arg(ap, int); ++fmt; }
                else while(*fmt >= '0' && *fmt <= '9') { state.arg.max_width = state.arg.max_width*10 + (*fmt++ - '0'); }
            }
            while(*fmt == 'l') { ++fmt; ++state.arg.longs; }
            if(*fmt == 'z') { ++fmt; state.arg.longs = 3; }
            switch(*fmt)
            {
                case '\0': goto unexpected;
                case 'n':
                {
                    *va_arg(ap, int*) = state.param - param;
                    break;
                }
                case 's':
                {
                    const char* str = va_arg(ap, const char*);
                    state.format_string(str, std::strlen(str));
                    break;
                }
                case 'c':
                {
                    state.numbuffer[0] = va_arg(ap, int);
                    state.format_string(state.numbuffer, 1);
                    break;
                }
                case 'p': { state.format_string(spacebuffer+15, 2);/*0x*/ state.arg.longs = (sizeof(void*)&8) ? 2 : 0; /*passthru hex*/ }
                case 'x': { state.arg.base = argument::hex;   goto got_int; }
                case 'X': { state.arg.base = argument::hexup; goto got_int; }
                case 'o': { state.arg.base = argument::oct;   goto got_int; }
                case 'b': if(SUPPORT_BINARY_FORMAT) { state.arg.base = argument::bin;   goto got_int; } else { goto got_int; }
                default:
                {
                got_int:;
                    std::int_fast64_t value = 0;
                    bool      uns   = state.arg.base != argument::decimal || *fmt == 'u';
                    switch(state.arg.longs)
                    {
                        case 0: value = va_arg(ap, int);          if(uns) { value = (unsigned)value;      } break;
                        case 1: value = va_arg(ap, long);         if(uns) { value = (unsigned long)value; } break;
                        case 2: value = va_arg(ap, long long);    uns = true; break;
                        case 3: value = va_arg(ap, ssize_t); if(uns) { value = (std::size_t)value; } break;
                    }
                    state.format_string(state.numbuffer, state.format_integer(value, uns));
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
