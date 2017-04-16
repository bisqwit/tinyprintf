#include <string>
#include "printf-c.cc"

static const char flags[][6] = {
    "",
    "-", // leftalign
    "0", // zeropad
    "+", // sign
    " ", // space for positive sign
    "#", // alt format
    "-0", "-+", "- ", "-#",
    "0+", "0 ", "0#",
    "+ ", " #",
    "-0+", "-0 ", "-0#",
    "-+ ", "- #",
    "0+ ", "0+#", "+ #",
    "-0+ ", "-0+#", "-+ #", "0+ #", "-0+ #",

    "--", "00", "++", "  ", "##",
    "-00", "-++", "- - ", "0  -#", "--+# ", "--0",
};

static void PrintParams() { }
template<typename... Params> static void PrintParams(int arg, Params... rest);
template<typename... Params> static void PrintParams(long arg, Params... rest);
template<typename... Params> static void PrintParams(long long arg, Params... rest);
template<typename... Params> static void PrintParams(long double arg, Params... rest);
template<typename... Params> static void PrintParams(double arg, Params... rest);
template<typename... Params> static void PrintParams(const void* arg, Params... rest);
template<typename... Params> static void PrintParams(const char* arg, Params... rest);

template<typename... Params>
static void PrintParams(int arg, Params... rest)
{
    std::printf(", %d", arg);
    PrintParams(rest...);
}
template<typename... Params>
static void PrintParams(long arg, Params... rest)
{
    std::printf(", %ldl", arg);
    PrintParams(rest...);
}
template<typename... Params>
static void PrintParams(long long arg, Params... rest)
{
    std::printf(", %lldll", arg);
    PrintParams(rest...);
}
template<typename... Params>
static void PrintParams(long double arg, Params... rest)
{
    std::printf(", %#.14Lgl", arg);
    PrintParams(rest...);
}
template<typename... Params>
static void PrintParams(double arg, Params... rest)
{
    std::printf(", %#.14g", arg);
    PrintParams(rest...);
}
template<typename... Params>
static void PrintParams(const void* arg, Params... rest)
{
    if(arg) std::printf(", (const void*)0x%llX", (unsigned long long)arg);
    else    std::printf(", nullptr");
    PrintParams(rest...);
}
template<typename... Params>
static void PrintParams(const char* arg, Params... rest)
{
    if(arg) std::printf(", \"%s\"", arg);
    else    std::printf(", nullptr");
    PrintParams(rest...);
}

static unsigned tests_failed = 0, tests_run = 0;

template<typename... Params>
void RunTest(const std::string& formatstr, Params... params)
{
    char result1[1024]{};
    char result2[1024]{};
    int out1 = __wrap_sprintf(result1, formatstr.c_str(), params...);
    int out2 = std::sprintf(  result2, formatstr.c_str(), params...);
    if(out1 != out2 || std::strcmp(result1, result2))
    {
        std::printf("printf(\"%s\"", formatstr.c_str());
        PrintParams(params...);
        std::printf(");\n");
        std::printf("- tiny: %d [%s]\n", out1, result1);
        std::printf("- std:  %d [%s]\n", out2, result2);
        ++tests_failed;
    }
    ++tests_run;
}
extern "C" {
int _write(int,const unsigned char*,unsigned,unsigned) { return 0; }
}

int main()
{
    RunTest("%%");
    for(int wid1mode = 0; wid1mode <= 2; ++wid1mode)
    for(int wid2mode = 0; wid2mode <= 2; ++wid2mode)
    for(auto flag: flags)
    {
        for(int wid1 = -22; wid1 <= 22; ++wid1)
        for(int wid2 = -22; wid2 <= 22; ++wid2)
        {
            if(!wid1mode && wid1) continue;
            if(!wid2mode && wid2) continue;
            if(wid1mode==1 && wid1<0) continue;
            if(wid2mode==1 && wid2<0) continue;

            auto test = [&](const char* format, auto param)
            {
                const char format_char = std::strchr(format, '\0')[-1];
                const bool zero_pad    = std::strchr(flag, '0');
                if(zero_pad && (format_char=='s' || format_char=='c'))
                {
                    // Skip undefined test (zeropad on strings and chars)
                    return;
                }
                /*if(wid2 == 0 && format_char == 'c')
                {
                    // This differs from libc.
                    return;
                }*/
                if(wid2mode && !param && (format_char == 's' || format_char == 'p'))
                {
                    // This differs from libc (possibly undefined)
                    // E.g. printf("%.3s", "")
                    //        Libc prints "(null)", we print "(nu"
                    return;
                }
                /*if(zero_pad && wid1 && wid2)
                {
                    // libc has a bug in this, testing against libc is meaningless
                    // E.g. printf("%03.1d", 3)
                    //        Libc prints "  3", we print "003"
                    return;
                }*/
                if((format_char=='e' || format_char=='f' || format_char=='g' || format_char=='a'
                 || format_char=='E' || format_char=='F' || format_char=='G' || format_char=='A') && wid2>=16)
                {
                    return;
                }

                std::string FormatStr;
                //FormatStr += '{';
                FormatStr += '%';
                FormatStr += flag;
                if(wid1mode)
                {
                    if(wid1mode == 1)      FormatStr += std::to_string(wid1);
                    else                   FormatStr += '*';
                }
                if(wid2mode)
                {
                    FormatStr += '.';
                    if(wid2mode == 1)      FormatStr += std::to_string(wid2);
                    else                   FormatStr += '*';
                }
                FormatStr += format;
                //FormatStr += '}';
                if(wid1mode == 2 && wid2mode == 2) RunTest(FormatStr, wid1, wid2, param);
                else if(wid1mode == 2)             RunTest(FormatStr, wid1, param);
                else if(wid2mode == 2)             RunTest(FormatStr, wid2, param);
                else                               RunTest(FormatStr, param);
            };
            test("s", (const char*)nullptr);
            test("s", (const char*)"");
            test("s", (const char*)"quix");
            test("s", (const char*)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
            test("c", int('\0'));
            test("c", int('A'));
            test("c", int(228-256));
            test("p", (const void*)nullptr);
            test("p", (const void*)0x12345678);
            test("d", int(0));
            test("d", int(-600000));
            test("d", int(600000));
            test("u", int(0));
            test("u", int(-600000));
            test("u", int(600000));
            test("ld", long(0));
            test("ld", long(-600000));
            test("ld", long(600000));
            test("lu", long(0));
            test("lu", long(-600000));
            test("lu", long(600000));
            test("lld", (long long)(0));
            test("lld", (long long)(-60000000000000ll));
            test("lld", (long long)(60000000000000ll));
            test("llu", (long long)(0));
            test("llu", (long long)(-60000000000000ll));
            test("llu", (long long)(60000000000000ll));
            test("x", 600000);
            test("X", 600000);
            test("o", 600000);
            test("x", -600000);
            test("X", -600000);
            test("o", -600000);
            test("x", 0);
            test("X", 0);
            test("o", 0);
            if(SUPPORT_H_LENGTHS)
            {
                test("hd", int(0));
                test("hd", int(20000));
                test("hd", int(-20000));
                test("hd", int(-600000));
                test("hd", int(600000));
                test("hu", int(0));
                test("hu", int(20000));
                test("hu", int(-20000));
                test("hu", int(-600000));
                test("hu", int(600000));
                test("hhd", int(0));
                test("hhd", int(100));
                test("hhd", int(-100));
                test("hhd", int(-600000));
                test("hhd", int(600000));
                test("hhu", int(0));
                test("hhu", int(100));
                test("hhu", int(-100));
                test("hhu", int(-600000));
                test("hhu", int(600000));
            }
            if(SUPPORT_FLOAT_FORMATS)
            {
                test("e", 10000.);
                test("e", 0.000123456789);
                test("e", 1.23456789);
                test("e", 123456.789);
                test("e", 1.5e42);
                test("e", 1.5e-42);
                test("e", 9.9999);
                test("e", 0.);
                test("e", 1./0.);
                test("f", 10000.);
                test("f", 0.000123456789);
                test("f", 1.23456789);
                test("f", 123456.789);
                test("f", 1.5e6);
                test("f", 1.5e-6);
                test("f", 9.9999);
                test("f", 0.);
                test("f", 1./0.);
            }
        }
    }
    std::printf("%u tests run, %u tests failed\n", tests_run, tests_failed);
}
