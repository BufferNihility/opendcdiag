/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <errno.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include "cpu_features.h"
#include "sandstone_p.h"

#ifndef signature_INTEL_ebx     /* cpuid.h lacks include guards */
#  include <cpuid.h>
#endif

static const size_t x86_locator_count = sizeof(x86_locators) / sizeof(x86_locators[0]);

#define CPU_BASIC_INFO_MAX_BRAND ((0x80000004 - 0x80000002) + 1)

struct cpu_basic_info {
    uint64_t features;
    uint8_t family;         ///! CPU family (usually 6)
    uint8_t stepping;       ///! CPU stepping
    uint16_t model;         ///! CPU model
    char brand[(CPU_BASIC_INFO_MAX_BRAND * 16) + 1];
};

/**
 * Called from detect_cpu(). The default weak implementation performs no
 * checks, just returns. Feel free to implement a strong version elsewhere if
 * you prefer the framework to check for system or CPU criteria and exit() if
 * those criteria are not met.
 */
void is_system_supported(const struct cpu_basic_info *);

__attribute__((weak))
void is_system_supported(const struct cpu_basic_info *basic_info) { }

#ifdef __APPLE__
/*
 * Apple's Darwin kernel for MacOS does not enable AVX-512 state-saving by
 * default in the XCR0 register, so it can catch AVX-512-using code via #UD and
 * allocate the state on-demand, per thread. So we need to check if the kernel
 * knows about AVX-512 in the first place.
 *
 * There's currently no released, Open Source code for AMX we can check.
 */

/* from https://github.com/apple/darwin-xnu/blob/xnu-4903.221.2/osfmk/i386/cpu_capabilities.h */
#define        kHasAVX512F                0x0000004000000000ULL
#define _COMM_PAGE32_BASE_ADDRESS        ( 0xffff0000 )                                /* base address of allocated memory */
#define _COMM_PAGE32_START_ADDRESS        ( _COMM_PAGE32_BASE_ADDRESS )        /* address traditional commpage code starts on */
#define _COMM_PAGE64_BASE_ADDRESS        ( 0x00007fffffe00000ULL )   /* base address of allocated memory */
#define _COMM_PAGE64_START_ADDRESS        ( _COMM_PAGE64_BASE_ADDRESS )        /* address traditional commpage code starts on */
#if defined(__i386__)
#define _COMM_PAGE_START_ADDRESS        _COMM_PAGE32_START_ADDRESS
#else
#define _COMM_PAGE_START_ADDRESS        _COMM_PAGE64_START_ADDRESS
#endif
#define _COMM_PAGE_CPU_CAPABILITIES64        (_COMM_PAGE_START_ADDRESS+0x010)        /* uint64_t _cpu_capabilities */

static uint64_t adjusted_xcr0(uint64_t xcr0, uint64_t xcr0_wanted)
{
    if (xcr0_wanted & XSave_Avx512State) {
        uint64_t capab = *(uint64_t *)_COMM_PAGE_CPU_CAPABILITIES64;
        if (capab & kHasAVX512F)
            xcr0 |= XSave_Avx512State;
    }
    return xcr0;
}
#elif defined(__linux__)
#  include <sys/syscall.h>
#  include <asm/prctl.h>
#  ifndef ARCH_GET_XCOMP_SUPP
#    define ARCH_GET_XCOMP_SUPP     0x1021
#    define ARCH_GET_XCOMP_PERM     0x1022
#    define ARCH_REQ_XCOMP_PERM     0x1023
#  endif // ARCH_GET_XCOMP_SUPP
static uint64_t adjusted_xcr0(uint64_t xcr0, uint64_t xcr0_wanted)
{
    static const uint64_t KernelNonDynamicXSave = XSave_Xtilecfg - 1;

    // Linux doesn't hide XCR0 bits
    xcr0 &= xcr0_wanted;

    // Check if we need to make a dynamic XSAVE request
    if ((xcr0_wanted & ~KernelNonDynamicXSave) == 0)
        return xcr0;            // no, XCR0 is accurate

    // dynamic XSAVE support required, ask for everything
    uint64_t feature_nr = 63 - __builtin_clzll(xcr0_wanted);
    if (syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, feature_nr) == 0)
        return xcr0_wanted;     // we got it

    // Either ARCH_REQ_XCOMP_PERM isn't supported or the kernel doesn't support
    // XSAVE'ing the feature we asked for (and we can't tell from the errno, since
    // it returns EINVAL for both situations). Ask the kernel what it does support.
    uint64_t xcr0_supported;
    if (syscall(SYS_arch_prctl, ARCH_GET_XCOMP_SUPP, &xcr0_supported) == 0) {
        // The call is supported (Linux >= 5.16). Ask for the highest bit that
        // we want and the kernel supports.
        xcr0_wanted &= xcr0_supported;
        feature_nr = 63 - __builtin_clzll(xcr0_wanted);
        if (syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, feature_nr) == 0)
            return xcr0_wanted;     // we got it
    }

    // Either Linux < 5.16 or the kernel failed to enable what it told us it
    // supported (can happen if something else has installed a sigaltstack()
    // that is too small).
#  ifndef LINUX_COMPAT_PRE_5_16_AMX_SUPPORT
    xcr0 &= KernelNonDynamicXSave;
#  endif
    return xcr0;
}
#else
static uint64_t adjusted_xcr0(uint64_t xcr0, uint64_t xcr0_wanted)
{
    // no adjustments in this OS
    (void)xcr0_wanted;
    return xcr0;
}
#endif

static uint64_t parse_register(enum X86CpuidLeaves leaf, uint32_t reg)
{
    size_t i;
    uint64_t features = 0;
    for (i = 0; i < x86_locator_count; ++i) {
        uint32_t locator = x86_locators[i];
        if (locator < leaf * 32 || locator >= (leaf + 1) * 32)
            continue;
        if (reg & (1U << (locator % 32)))
            features |= UINT64_C(1) << i;
    }
    return features;
};

#if SANDSTONE_NO_LOGGING
#  define cpuid_errmsg(id, msg)         logging_i18n(LOG_LEVEL_QUIET, id)
#else
#  define cpuid_errmsg(id, msg)         fputs(msg, stderr)
#endif

__attribute__((cold, noreturn))
static void detect_cpu_not_supported(const char *msg)
{
    cpuid_errmsg(MSG_OS_Not_Supported, msg);
    exit(EX_CONFIG);
}

__attribute__((noinline))
static void detect_cpu(struct cpu_basic_info *basic_info)
{
    uint32_t eax, ebx, ecx, edx;
    uint32_t max_level = 0;
    uint64_t features = 0;

    __cpuid(0, max_level, ebx, ecx, edx);

    eax = ebx = ecx = edx = 0;
    __cpuid(1, eax, ebx, ecx, edx);
    features |= parse_register(Leaf01ECX, ecx);
    features |= parse_register(Leaf01EDX, edx);
    unsigned family = ((eax >> 8) & 0xf) | ((eax >> (20-4)) & 0xff0);
    unsigned model = ((eax >> 4) & 0xf) | ((eax >> (16-4)) & 0xf0);
    unsigned stepping = eax & 0xf;

    bool osxsave = false;
    if (ecx & (1<<26)) {
        // CPU supports XSAVE
        osxsave = true;
        if ((ecx & (1<<27)) == 0)           // OSXSAVE
            detect_cpu_not_supported("This OS did not enable XSAVE support. Cannot run.\n");
    }

    if (max_level >= 7) {
        __cpuid_count(7, 0, eax, ebx, ecx, edx);
        features |= parse_register(Leaf07_00EBX, ebx);
        features |= parse_register(Leaf07_00ECX, ecx);
        features |= parse_register(Leaf07_00EDX, edx);

        __cpuid_count(7, 1, eax, ebx, ecx, edx);
        features |= parse_register(Leaf07_01EAX, eax);
    }
    if (max_level >= 13) {
        __cpuid_count(13, 1, eax, ebx, ecx, edx);
        features |= parse_register(Leaf13_01EAX, eax);
    }
    __cpuid(0x80000001, eax, ebx, ecx, edx);
    features |= parse_register(Leaf80000001hECX, ecx);

    if (osxsave) {
        uint64_t xcr0 = 0;
        uint64_t xcr0_wanted = 0;

        if (features & XSaveReq_AvxState)
            xcr0_wanted |= XSave_AvxState;
        if (features & XSaveReq_Avx512State)
            xcr0_wanted |= XSave_Avx512State;
        if (features & XSaveReq_AmxState)
            xcr0_wanted |= XSave_AmxState;

        if (xcr0_wanted) {
            uint32_t xcr0_low, xcr0_high;
            asm("xgetbv" : "=a" (xcr0_low), "=d" (xcr0_high) : "c" (0));
            xcr0 = xcr0_low;
            if (xcr0_wanted != (uint32_t)xcr0_wanted)
                xcr0 |= (uint64_t)xcr0_high << 32;      // don't discard %edx
            xcr0 = adjusted_xcr0(xcr0, xcr0_wanted);
        }

        // Check what XSAVE features this OS supports and we're allowed to use.
        // For AMX, we (currently) allow gracefully degrading. For AVX512 and
        // earlier, we stop execution.
        if ((xcr0 & XSave_AmxState) != XSave_AmxState) {
            xcr0_wanted &= ~XSave_AmxState;
            features &= ~XSaveReq_AmxState;
        }

        if (xcr0_wanted && (xcr0 & xcr0_wanted) != xcr0_wanted)
            detect_cpu_not_supported("This kernel did not enable necessary AVX or AMX state-saving."
                                     " Cannot run.\n");

    }

    __cpuid(0x80000000, eax, ebx, ecx, edx);
    if ((eax & 0x80000000) && (eax >= 0x80000004)) {
        uint32_t *ptr = (uint32_t*) &basic_info->brand[0];
        uint32_t i;
        for (i = 0; i < CPU_BASIC_INFO_MAX_BRAND; i++) {
            __cpuid(0x80000002 + i, eax, ebx, ecx, edx);
            *ptr++ = eax;
            *ptr++ = ebx;
            *ptr++ = ecx;
            *ptr++ = edx;
        }

        // Strictly speaking this isn't needed as CPUID should include a 0 at
        // the end of the brand string, but just to be absolutely sure, we add
        // an extra zero here (space has been reserved for it).
        *ptr++ = 0;
    } else {
        basic_info->brand[0] = 0;
    }

    basic_info->features = features;
    basic_info->family = family;
    basic_info->model = model;
    basic_info->stepping = stepping;
}

__attribute__((unused))
static void check_missing_features(uint64_t features, uint64_t minimum_cpu_features)
{
    uint64_t missing = minimum_cpu_features & ~features;
    if (!missing)
        return;

    size_t i;
    cpuid_errmsg(MSG_Processor_Required_Features,
                 "Cannot run on this CPU.\n"
                 "This application requires certain features not found in your CPU:");
    for (i = 0; i < x86_locator_count; ++i) {
        if (missing & (UINT64_C(1) << i))
            fputs(features_string + features_indices[i], stderr);
    }
    fputs("\nexit: invalid\n", stderr);
    exit(EX_CONFIG);
}

#undef cpuid_errmsg
