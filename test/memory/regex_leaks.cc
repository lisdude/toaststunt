// Invoke the real static builtins without starting a server or loading a database.
#include "../../src/pcre_moo.cc"
#include "garbage.h"
#include <sanitizer/lsan_interface.h>
#include <cassert>
#include <cstdio>
#include <string>

static void release_package(package p)
{
    if (p.kind == package::BI_RETURN)
        free_var(p.u.ret);
    else {
        assert(p.kind == package::BI_RAISE);
        free_var(p.u.raise.code);
        free_str(p.u.raise.msg);
        free_var(p.u.raise.value);
    }
}

__attribute__((noinline)) static void exercise(const std::string &mode)
{
    std::string subject = "abc";
    const char *pattern = "a";
    error expected = E_NONE;
    bool replace = mode.find("replace-") == 0;
    if (mode == "match-limit") {
        subject.assign(DEFAULT_LOOPS + 1, 'a');
        expected = E_MAXREC;
    } else if (mode == "match-error" || mode == "match-partial-error") {
        subject = std::string(80, 'a') + "!";
        pattern = "(*LIMIT_MATCH=1)(a+)+$";
        expected = E_INVARG;
        if (mode == "match-partial-error") {
            subject = "x" + subject;
            pattern = "(*LIMIT_MATCH=10)(?:x|(a+)+$)";
        }
    } else if (mode == "replace-error") {
        subject = std::string(80, 'a') + "!";
        pattern = "s/(*LIMIT_MATCH=1)(a+)+$/X/g";
        expected = E_INVARG;
    } else if (mode == "replace-ok") {
        pattern = "s/a/X/g";
    } else if (mode == "match-named") {
        pattern = "(?<letter>a)";
    } else if (mode == "match-none") {
        pattern = "z";
    } else if (mode == "match-empty") {
        subject.clear();
    } else if (mode == "match-compile-error") {
        pattern = "(";
        expected = E_INVARG;
    } else if (mode == "replace-compile-error") {
        pattern = "s/(/X/g";
        expected = E_INVARG;
    } else if (mode == "match-global" || mode == "match-single") {
        subject = "aaa";
    } else {
        assert(mode == "match-ok");
    }

    Var args = new_list(mode == "match-single" ? 4 : 2);
    args.v.list[1] = str_dup_to_var(subject.c_str());
    args.v.list[2] = str_dup_to_var(pattern);
    if (mode == "match-single") {
        args.v.list[3] = Var::new_int(0);
        args.v.list[4] = Var::new_int(0);
    }
    package result = replace ? bf_pcre_replace(args, 1, nullptr, 0)
                             : bf_pcre_match(args, 1, nullptr, 0);
    if (expected != E_NONE) {
        assert(result.kind == package::BI_RAISE);
        assert(result.u.raise.code.v.err == expected);
        if (mode == "match-error" || mode == "match-partial-error" || mode == "replace-error")
            assert(strstr(result.u.raise.msg, "(-47)") != nullptr);
    } else {
        assert(result.kind == package::BI_RETURN);
        if (replace) {
            assert(result.u.ret.type == TYPE_STR);
            assert(strcmp(result.u.ret.v.str, "Xbc") == 0);
        } else {
            assert(result.u.ret.type == TYPE_LIST);
            int count = (mode == "match-none" || mode == "match-empty") ? 0
                      : mode == "match-global" ? 3 : 1;
            assert(result.u.ret.v.list[0].v.num == count);
        }
    }
    release_package(result);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    register_pcre();
    for (int i = 0; i < std::stoi(argv[2]); ++i)
        exercise(argv[1]);
    gc_collect();
    pcre_shutdown();
    gc_collect();
    // A separate noinline frame keeps dead builtin locals out of the root set.
    int leaked = __lsan_do_recoverable_leak_check();
    std::printf("%s: %s\n", argv[1], leaked ? "LEAK" : "PASS");
    return leaked ? 23 : 0;
}
