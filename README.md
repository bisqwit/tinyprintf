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

* Minimal memory use (there is only a single 23-byte array and a few assorted temporary variables)
  * Memory use is increased to 65 bytes+data if binary formats are enabled
* The following format types are supported and have the same meaning as in GNU libc printf: `n`, `s`, `c`, `p`, `x`, `X`, `o`, `d`, `u`, and `i`
  * Format `b` is only enabled if a SUPPORT_BINARY_FORMAT is set
* The following length modifiers are supported and have the same meaning as in GNU libc printf: none, `h`, `hh`, `l`, `ll`, `L`, `j`, `z`, and `t`
  * The length modifier also affects the pointer type in `n` format
* Min-width, min-precision, max-width, sign, space, justification modifiers are supported according to the standard
* Re-entrant code (e.g. it is safe to call `sprintf` within your stream I/O function invoked by `printf`)
* Compatible with GCC’s optimizations where e.g. `printf("abc\n")` is automatically converted into `puts("abc")`

## Caveats

* Stream I/O errors are not handled
* No buffering; all text is printed as soon as available, resulting in multiple calls of the I/O function (but as many bytes are printed with a single call as possible)
* No file I/O: printing is only supported into a predefined output (such as through serial port), or into a string. Any `FILE*` pointer parameters are completely ignored
* Data is never copied. Any pointers into strings are expected to be valid throughout the call to the printing function
* `snprintf`, `vsnprintf`, `dprintf`, `vdprintf`, `asprintf`, and `vasprintf` are not included yet
  * Neither are `wprintf`, `fwprintf`, `swprintf`, `vwprintf`, `vfwprintf`, `vswprintf` etc.
* Padding/cutting widths are limited to 65534 characters
  * The minimum integer digits format modifier (such as %.10d) is limited to 22 digits (or 64 if SUPPORT_BINARY_FORMAT=true)
* Behavior differs to GNU libc printf when a nul pointer is printed with `p` or `s` formats and max-width specifier is used
  
## Unsupported syntax

Note that any of the following traits may change in future releases.

* Positional parameters (e.g. `%5$d`) are not supported
* Floating point formats (`e`, `E`, `f`, `F`, `g`, `G`, `a`, and `A`) are not supported
* The `'` and `I` flag characters are not supported
* Length modifiers are ignored for `s`, `c` and `p` format types
  * I.e. `wchar_t` strings or `wint_t` chars are not supported.
* Flags and width modifiers are ignored for the `n` format type
* Any other format type than `n`, `s`, `c`, `p`, `x`, `X`, `o`, or `b` is treated as if `d` was used
  * `d` and `i` are equivalent and have the same meaning
  * This includes unsupported combinations and permutations of length modifier letters

## Rationale

* This module was designed for use with mbed-enabled programming, and to remove any dependencies to stdio (specifically FILE stream facilities) in the linkage, reducing the binary size.
