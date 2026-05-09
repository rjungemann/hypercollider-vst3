The failure is caused by a WAMR Windows source bug under MinGW/GCC, not by your plugin code.
Root cause

The build stops while compiling WAMR’s Windows platform file:

    Failing file: build/_deps/wamr-src/core/shared/platform/windows/win_file.c
    Error area: around lines 1298–1313 in the log
    Key symptom:
        warning: multi-line comment [-Wcomment]
        then parse errors like expected identifier before '(' token

The trigger is the comment text in WAMR’s Windows path handling code:
C

// Starts with \\??\\

and similar lines like:
C

// \\??\\<drive>:\

With GCC/MinGW, the trailing backslashes inside // comments can escape the newline during translation, causing the next line to be treated as part of the comment and breaking the parser. That matches the exact cascade of syntax errors in the log.
Best solution
1. Pin WAMR to a revision that does not contain this broken code

You currently fetch:

CMakeLists.txt
CMake

FetchContent_Declare(
    wamr
    GIT_REPOSITORY https://github.com/bytecodealliance/wasm-micro-runtime.git
    GIT_TAG WAMR-2.4.3
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

Update GIT_TAG to a newer WAMR release or commit that fixes win_file.c.

Suggested change:
CMake

FetchContent_Declare(
    wamr
    GIT_REPOSITORY https://github.com/bytecodealliance/wasm-micro-runtime.git
    GIT_TAG WAMR-2.4.4
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

If 2.4.4 is not available or still fails, pin to a known-good commit instead:
CMake

FetchContent_Declare(
    wamr
    GIT_REPOSITORY https://github.com/bytecodealliance/wasm-micro-runtime.git
    GIT_TAG <known-good-commit-sha>
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

This is the cleanest fix because it avoids maintaining a local patch to dependency code.
Practical fallback if you must stay on WAMR-2.4.3

Patch the offending comments in win_file.c so they do not end with backslashes.

Examples of safe replacements:
C

/* Starts with \\??\\ */

C

/* \\??\\<drive>:\ */

C

/* \\??\\UNC\\<server>\\<share>\\ - make sure the final path looks like \\\\<server>\\<share>\\ */

Better yet, avoid trailing backslashes entirely:
C

/* Starts with NT path prefix \\??\\ */

C

/* Drive path form: \\??\\C:\\ */

C

/* UNC path form: \\??\\UNC\\server\\share\\ */

The issue is specifically the lexical handling of backslash-newline, so rewriting comments is enough.
How to integrate the fallback patch in your build

Since WAMR is fetched with FetchContent, you can apply a small patch after download and before inclusion, or vendor the dependency at a fixed revision.

If you want a minimal CMake-side workaround, switch from FetchContent_MakeAvailable(wamr) to populating it first, patching the file, then adding it.

Conceptually:
CMake

include(FetchContent)

FetchContent_Declare(
    wamr
    GIT_REPOSITORY https://github.com/bytecodealliance/wasm-micro-runtime.git
    GIT_TAG WAMR-2.4.3
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

FetchContent_GetProperties(wamr)
if(NOT wamr_POPULATED)
    FetchContent_Populate(wamr)

    if(WIN32)
        set(WAMR_WIN_FILE "${wamr_SOURCE_DIR}/core/shared/platform/windows/win_file.c")
        file(READ "${WAMR_WIN_FILE}" WAMR_WIN_FILE_CONTENTS)

        string(REPLACE "// Starts with \\\\??\\\\"
                       "/* Starts with NT path prefix \\\\??\\\\ */"
                       WAMR_WIN_FILE_CONTENTS
                       "${WAMR_WIN_FILE_CONTENTS}")

        string(REPLACE "// \\\\??\\\\<drive>:\\"
                       "/* Drive path form: \\\\??\\\\C:\\\\ */"
                       WAMR_WIN_FILE_CONTENTS
                       "${WAMR_WIN_FILE_CONTENTS}")

        string(REPLACE "// \\\\??\\\\UNC\\\\<server>\\\\<share>\\\\ - make sure the final path looks like \\\\\\\\<server>\\\\<share>\\\\"
                       "/* UNC path form: \\\\??\\\\UNC\\\\server\\\\share\\\\ */"
                       WAMR_WIN_FILE_CONTENTS
                       "${WAMR_WIN_FILE_CONTENTS}")

        file(WRITE "${WAMR_WIN_FILE}" "${WAMR_WIN_FILE_CONTENTS}")
    endif()

    add_subdirectory("${wamr_SOURCE_DIR}" "${wamr_BINARY_DIR}")
endif()

That said, upgrading/pinning WAMR is preferable.
Job-definition context

The failure happens in the Windows matrix build from:

 .github/workflows/plugin-build.yml

This job builds on windows-2022, and the configure step uses plain CMake + Ninja:

plugin-build.yml

So the failure is reproducible during dependency compilation before your plugin targets are even built.
Recommended code change

Edit CMakeLists.txt to move off WAMR-2.4.3 first:
CMake

FetchContent_Declare(
    wamr
    GIT_REPOSITORY https://github.com/bytecodealliance/wasm-micro-runtime.git
    GIT_TAG WAMR-2.4.4
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

If you need a robust short-term workaround, add a Windows-only patch step for core/shared/platform/windows/win_file.c that rewrites the trailing-backslash comments.