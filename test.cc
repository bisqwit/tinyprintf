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
    "-0+ ", "-0+#", "-+ #", "0+ #", "-0+ #"
};

static void PrintParams()
{
}
template<typename... Params> static void PrintParams(int arg, Params... rest);
template<typename... Params> static void PrintParams(long arg, Params... rest);
template<typename... Params> static void PrintParams(long long arg, Params... rest);
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
    }
}
extern "C" {
int _write(int,const unsigned char*,unsigned,unsigned) { return 0; }
}

int main()
{
        for(int wid1mode = 0; wid1mode <= 2; ++wid1mode)
        for(int wid2mode = 0; wid2mode <= 2; ++wid2mode)
    for(auto flag: flags)
    {
        for(int wid1 = -20; wid1 <= 20; ++wid1)
        for(int wid2 = -20; wid2 <= 20; ++wid2)
        {
            if(!wid1mode && wid1) continue;
            if(!wid2mode && wid2) continue;
            if(wid1mode==1 && wid1<0) continue;
            if(wid2mode==1 && wid2<0) continue;

            auto test = [&](const char* format, auto param)
            {
                if(std::strchr(flag, '0') && (std::strchr(format, 's') || std::strchr(format, 'c')))
                {
                    // Skip undefined test
                    return;
                }
                if(wid2 == 0 && !std::strchr(format, 's'))
                {
                    // This differs from libc.
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
    }
}

