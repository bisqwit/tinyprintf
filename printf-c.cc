#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdint>

static constexpr bool SUPPORT_BINARY_FORMAT = false;
static constexpr bool STRICT_COMPLIANCE     = true;

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

    static constexpr unsigned PatternLength = 8;
    static const char spacebuffer[PatternLength*2 + 3 + 4+2+2+5+6] {
        // eight spaces
        ' ',' ',' ',' ', ' ',' ',' ',' ',
        // eight zeros
        '0','0','0','0', '0','0','0','0',
        // first three prefixes
        '-','+',' ',
        // last three prefixes
        0x24,0x26,0x58,0x6D,
        '0','x', '0','X', '(','n','i','l',')', '(','n','u','l','l',')'
    };

    static constexpr unsigned char prefix_minus = 0x1;
    static constexpr unsigned char prefix_plus  = 0x2;
    static constexpr unsigned char prefix_space = 0x3;
    static constexpr unsigned char prefix_singlechar = 0x3;
    static constexpr unsigned char prefix_0x    = 1*4; // 0x4
    static constexpr unsigned char prefix_0X    = 2*4; // 0x8
    static constexpr unsigned char prefix_nil   = 3*4; // 0xC
    static constexpr unsigned char prefix_null  = 4*4; // 0x10
    static constexpr unsigned char prefix_multichar = ~prefix_singlechar;

    struct prn
    {
        char* param;
        void (*put)(char*,const char*,std::size_t);

        const char* putbegin = nullptr;
        const char* putend   = nullptr;

        argument arg;

        char numbuffer[SUPPORT_BINARY_FORMAT ? 64 : 22];
        unsigned char prefix_index = 0;

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
        unsigned format_integer(intfmt_t value, unsigned char sign_mode)
        {
            // Maximum length is ceil(log8(2^64)) = ceil(64/3) = 22 characters
            static_assert(sizeof(numbuffer) >= (SUPPORT_BINARY_FORMAT ? 64 : 22), "Too small numbuffer");

            if(STRICT_COMPLIANCE && unlikely((sign_mode & 2) && !value)) // (nil) and %p
            {
                prefix_index = prefix_nil; // Discards other prefixes
                return 0;
            }
            if(sign_mode != 0) // Pointers and signed values
            {
                bool negative = value < 0;
                if(negative)       { value = -value; prefix_index |= prefix_minus; }
                else if(arg.sign)  {                 prefix_index |= prefix_plus;  }
                else if(arg.space) {                 prefix_index |= prefix_space; }
                // GNU libc printf ignores '+' and ' ' modifiers on unsigned formats,
                // but curiously, not for %p
                // Note that '+' overrides ' ' if both are used.
            }

            uintfmt_t uvalue = value;
            unsigned base = arg.base & 63;
            char     lett = ((arg.base & 64) ? 'A' : 'a')-10;

            unsigned width = 0;
            for(uintfmt_t uvalue_test = uvalue; uvalue_test != 0 || (!STRICT_COMPLIANCE && width==0); )
            {
                ++width;
                uvalue_test /= base;
            }
            if(unlikely(arg.alt) && likely(uvalue != 0))
            {
                switch(base)
                {
                    case 8:  width += 1; break; // Add '0' prefix
                    case 16: prefix_index |= (arg.base & 64) ? prefix_0X : prefix_0x; break; // Add 0x/0X prefix
                }
            }
            // For integers, the length limit (.xx) has a different meaning:
            // Minimum number of digits printed.
            if(unlikely(arg.max_width != 65535))
            {
                if(arg.max_width > width) width = arg.max_width; // width can only grow here.
                arg.max_width = 65535;
                // This setting clears out zeropadding according to standard
                if(STRICT_COMPLIANCE) { arg.zeropad = false; }
            }
            else if(STRICT_COMPLIANCE && width == 0)
            {
                // Zero-width is permitted if explicitly specified,
                // but otherwise we always print at least 1 digit.
                width = 1;
            }

            // Range check
            if(unlikely(width > sizeof(numbuffer))) width = sizeof(numbuffer);

            char* target = numbuffer + width;
            unsigned length = target - numbuffer;
            while(width-- > 0)
            {
                unsigned digitvalue = uvalue % base; uvalue /= base;
                *--target = digitvalue + (digitvalue < 10 ? '0' : lett);
            }
            return length;
        }
        void append_spaces(const char* from, unsigned number) NOINLINE
        {
            while(number > 0)
            {
                unsigned n = number;
                if(n > PatternLength) n = PatternLength;
                append(from, n);
                number -= n;
            }
        }
        void format_string(const char* source, unsigned length) NOINLINE
        {
            if(unlikely(source == nullptr)) { length = 0; prefix_index = prefix_null; }
            /* There are three possible combinations:
             *
             *    Leftalign      prefix, source, spacebuffer
             *    Zeropad        prefix, spacebuffer, source
             *    neither        spacebuffer, prefix, source
             *
             * Note that in case of zeropad+leftalign,
             * zeropad is disregarded according to the standard.
             */
            char prefixbuffer[6];
            unsigned prefixlen = 0; // Max length: "+(nil)" or "(null)" = 6 chars
            if(prefix_index & prefix_singlechar)
            {
                prefixbuffer[prefixlen++] = spacebuffer[PatternLength*2 + (prefix_index%4)-1];
            }
            if(prefix_index & prefix_multichar)
            {
                unsigned char ctrl = spacebuffer[PatternLength*2+3+prefix_index/4-1];
                std::memcpy(prefixbuffer+prefixlen, spacebuffer+PatternLength*2+3+(ctrl&0xF), (ctrl>>4));
                prefixlen += (ctrl>>4);
                /*std::printf("prefix=%02X ctrl=%02X s=%.3s prefixbuf=<%.*s>\n",
                    prefix_index,ctrl, spacebuffer+16+3+(ctrl&0xF),
                    int(prefixlen), prefixbuffer);*/
            }

            // Calculate length of prefix + source
            unsigned combined_length = length + prefixlen;
            // Clamp it into maximum permitted width
            if(combined_length > arg.max_width)
            {
                combined_length = arg.max_width;
                // Figure out how to divide this between prefix and source
                if(unlikely(combined_length <= prefixlen))
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
            if(arg.leftalign)
            {
                // Prefix, then source, then spacebuffer
                append(prefixbuffer, prefixlen);
                append(source, length);
                append_spaces(spacebuffer, pad);
            }
            else if(arg.zeropad && (!STRICT_COMPLIANCE || likely(prefix_index < 0xC))) // Disable zero-padding for (nil), (null)
            {
                // Prefix, then spacebuffer, then source
                append(prefixbuffer, prefixlen);
                append_spaces(spacebuffer+PatternLength, pad);
                append(source, length);
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
                    if(STRICT_COMPLIANCE)
                    {
                        state.arg.max_width = 65535; // Max-width has no effect on %c formats
                    }
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

                    state.format_string(state.numbuffer, state.format_integer(value, *fmt=='p' ? 2 : (uns ? 0 : 1)));
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

    static void mfunc(char* target, const char* src, std::size_t n)
    {
        //std::printf("mfunc(%p,%p,%zu)\n", target,src,n);
        for(std::size_t a=0; a<n; ++a) target[a] = src[a];
        //std::memcpy(target, src, n);
    }

    int __wrap_vsprintf(char* target, const char* fmt, std::va_list ap) USED_FUNC;
    int __wrap_vsprintf(char* target, const char* fmt, std::va_list ap)
    {
        int ret = myprintf::myvprintf(fmt, ap, target, mfunc);
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
