# tinyprintf

`printf-c.cc` is a C++ module that replaces certain C-language libc functions
with a tiny alternatives suitable for embedded programs.

The functions replaced are `printf`, `vprintf`, `fprintf`, `vfprintf`, `sprintf`, `vsprintf`, `fiprintf`, `puts`, `fputs`, `putchar`, `fflush`, and `fwrite`.

To use, compile `printf-c.cc` using your C++ compiler, and link it into your project.
You will have to add the following linker flags:

    -Wl,--wrap,printf  -Wl,--wrap,fprintf  -Wl,--wrap,sprintf  -Wl,--wrap,fiprintf    
    -Wl,--wrap,vprintf -Wl,--wrap,vfprintf -Wl,--wrap,vsprintf -Wl,--wrap,fflush    
    -Wl,--wrap,puts    -Wl,--wrap,putchar  -Wl,--wrap,fputs    -Wl,--wrap,fwrite    

GNU build tools are probably required.

Compiling with `-ffunction-sections -fdata-sections` and linking with `-Wl,--gc-sections` is strongly recommended.
Compiling with `-flto` is supported.
Compiling with `-fbuiltin` is supported.

*IMPORTANT*: You will have to edit printf-c.cc file, locate `wfunc`,
and replace that function with code that is suitable for your project.

## Features

* Memory usage is negligible (around 50-200 bytes of automatic storage used, depending on compiler optimizations and whether binary formats are enabled)
* The following format types are supported and have the same meaning as in GNU libc printf: `n`, `s`, `c`, `p`, `x`, `X`, `o`, `d`, `u`, and `i`
  * Format `b` (binary integer) is only enabled if a SUPPORT_BINARY_FORMAT is set
* The following length modifiers are supported and have the same meaning as in GNU libc printf: none, `h`, `hh`, `l`, `ll`, `L`, `j`, `z`, and `t`
  * The length modifier also affects the pointer type in `n` format
  * It is possible to explicitly disable most of these modifiers to reduce binary size
* Min-width, min-precision, max-width, sign, space, justification modifiers are supported and fully standards-compliant (C99 / C++11)
* Re-entrant code (e.g. it is safe to call `sprintf` within your stream I/O function invoked by `printf`)
  * Thread-safe as long as your wfunc is thread-safe. `printf` calls are not locked, so prints from different threads can interleave.
* Compatible with GCC’s optimizations where e.g. `printf("abc\n")` is automatically converted into `puts("abc")`
* Positional parameters are fully supported (e.g. `printf("%2$s %1$0*3$ld", 5L, "test", 4);` works and prints “test 0005”), disabled by default
  * If positional parameters are enabled and used, a dynamically allocated array is used to temporarily hold parameter information. The size of the array is `number-of-printf-parameters * (sizeof(short) + sizeof(largest-param))`, where largest-param is the largest printfable parameter supported by the enabled options (one of `int`, `long`, `long long`, `void*`, `double`, and `long double`). For instance, if the largest possible type is `long long`, `long long` is 8 bytes, a printf with 5 parameters will temporarily allocate 5 * (2+8) = 50 bytes of dynamic storage.
  * If positional parameters are enabled but not used in the format string, no dynamic allocation occurs, but the format string is still scanned twice.
* The `n` format can be disabled by changing SUPPORT_N_FORMAT to false

## Caveats

* Stream I/O errors are not handled
* No buffering; all text is printed as soon as available, resulting in multiple calls of the I/O function (but as many bytes are printed with a single call as possible)
* No file I/O: printing is only supported into a predefined output (such as through serial port), or into a string. Any `FILE*` pointer parameters are completely ignored
* Data is never copied. Any pointers into strings are expected to be valid throughout the call to the printing function
  * If positional parameters are enabled, parameters (pointers, but not the pointed data) are copied into a temporary dynamically allocated array though
* `snprintf`, `vsnprintf`, `dprintf`, `vdprintf`, `asprintf`, and `vasprintf` are not included yet
  * Neither are `wprintf`, `fwprintf`, `swprintf`, `vwprintf`, `vfwprintf`, `vswprintf` etc.
* Padding/cutting widths are limited to 134217727 characters (2²⁷−1)
  * The minimum integer digits format modifier (such as %.10d) is limited to 22 digits (or 64 if SUPPORT_BINARY_FORMAT=true)
* Behavior differs to GNU libc printf when a nul pointer is printed with `p` or `s` formats and max-width specifier is used
* If positional parameters are enabled, there may be a maximum of 32767 parameters to printf.

## Unsupported syntax

Note that any of the following traits may change in future releases.

* The `'` and `I` flag characters are not supported
* Length modifiers such as `l` or `z` are ignored for `s`, `c` and `p` format types
  * I.e. `wchar_t` strings or `wint_t` chars are not supported.
* Flags and width modifiers are ignored for the `n` format type
* Any other format type than `n`, `s`, `c`, `p`, `x`, `X`, `o`, or `b` is treated as if `d` was used
  * `d` and `i` are equivalent and have the same meaning
  * This includes unsupported combinations and permutations of length modifier letters

## Known bugs

* Floating point support (formats `e`, `E`, `f`, `F`, `g`, `G`, `a`, and `A`) is all sorts of broken and disabled by default

## Rationale

* This module was designed for use with mbed-enabled programming, and to remove any dependencies to stdio (specifically FILE stream facilities) in the linkage, reducing the binary size.
  * And perhaps a tiny bit of “we do what we must, because we can”.

## Author

Copyright © Joel Yliluoma 2017. (http://iki.fi/bisqwit/)    
Distribution terms: MIT
