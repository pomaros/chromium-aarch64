// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// HWCapProbe: the kernel's CPU feature words on AArch64 AROS
// (KrnGetSystemAttr(KATTR_HWCap) and KATTR_HWCap2). Prints both words and
// the feature names, then executes one instruction of each optional
// extension the kernel reports that a codec relies on (CRC32, PMULL, the
// dot product) and checks its result, so a bit set for an absent feature
// faults instead of passing. FP and AdvSIMD are mandatory on every AROS
// AArch64 system. Prints [HWCAPPROBE] lines and result=PASS.

#include <stdint.h>
#include <stdio.h>

#include <aros/aarch64/hwcap.h>
#include <aros/kernel.h>
#include <proto/exec.h>
#include <proto/kernel.h>

void *KernelBase;

static const struct {
    uint64_t bit;
    const char *name;
} kCaps[] = {
    {AROS_HWCAP_FP, "fp"}, {AROS_HWCAP_ASIMD, "asimd"}, {AROS_HWCAP_AES, "aes"},
    {AROS_HWCAP_PMULL, "pmull"}, {AROS_HWCAP_SHA1, "sha1"}, {AROS_HWCAP_SHA2, "sha2"},
    {AROS_HWCAP_CRC32, "crc32"}, {AROS_HWCAP_ATOMICS, "atomics"}, {AROS_HWCAP_FPHP, "fphp"},
    {AROS_HWCAP_ASIMDHP, "asimdhp"}, {AROS_HWCAP_ASIMDRDM, "asimdrdm"},
    {AROS_HWCAP_JSCVT, "jscvt"}, {AROS_HWCAP_FCMA, "fcma"}, {AROS_HWCAP_LRCPC, "lrcpc"},
    {AROS_HWCAP_DCPOP, "dcpop"}, {AROS_HWCAP_SHA3, "sha3"}, {AROS_HWCAP_SM3, "sm3"},
    {AROS_HWCAP_SM4, "sm4"}, {AROS_HWCAP_ASIMDDP, "asimddp"}, {AROS_HWCAP_SHA512, "sha512"},
    {AROS_HWCAP_SVE, "sve"}, {AROS_HWCAP_ASIMDFHM, "asimdfhm"}, {AROS_HWCAP_DIT, "dit"},
    {AROS_HWCAP_ILRCPC, "ilrcpc"}, {AROS_HWCAP_FLAGM, "flagm"}, {AROS_HWCAP_SB, "sb"},
};

static const struct {
    uint64_t bit;
    const char *name;
} kCaps2[] = {
    {AROS_HWCAP2_SVE2, "sve2"}, {AROS_HWCAP2_FRINT, "frint"}, {AROS_HWCAP2_I8MM, "i8mm"},
    {AROS_HWCAP2_BF16, "bf16"}, {AROS_HWCAP2_RNG, "rng"},
};

static int failures;

static void check(const char *what, int ok)
{
    printf("[HWCAPPROBE] %s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static uint32_t crc32_soft(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (int i = 0; i < 8; i++)
        crc = (crc >> 1) ^ (0xedb88320U & -(crc & 1));
    return crc;
}

int main(void)
{
    KernelBase = OpenResource((CONST_STRPTR)"kernel.resource");
    if (!KernelBase) {
        printf("[HWCAPPROBE] no kernel.resource\n[HWCAPPROBE] result=FAIL\n");
        return 20;
    }
    intptr_t caps = KrnGetSystemAttr(KATTR_HWCap);
    intptr_t caps2 = KrnGetSystemAttr(KATTR_HWCap2);
    printf("[HWCAPPROBE] hwcap=0x%llx hwcap2=0x%llx\n", (unsigned long long)caps,
           (unsigned long long)caps2);
    check("attribute supported", caps != -1 && caps2 != -1);
    if (caps == -1 || caps2 == -1) {
        printf("[HWCAPPROBE] result=FAIL\n");
        return 20;
    }

    char names[512];
    int length = 0;
    for (unsigned i = 0; i < sizeof kCaps / sizeof kCaps[0]; i++)
        if (caps & kCaps[i].bit)
            length += snprintf(names + length, sizeof names - length, " %s", kCaps[i].name);
    for (unsigned i = 0; i < sizeof kCaps2 / sizeof kCaps2[0]; i++)
        if (caps2 & kCaps2[i].bit)
            length += snprintf(names + length, sizeof names - length, " %s", kCaps2[i].name);
    printf("[HWCAPPROBE] features:%s\n", names);

    check("fp and asimd", (caps & AROS_HWCAP_FP) && (caps & AROS_HWCAP_ASIMD));

    if (caps & AROS_HWCAP_CRC32) {
        uint32_t hard = 0xffffffffU, soft = 0xffffffffU;
        static const uint8_t data[] = "AROS";
        for (unsigned i = 0; i < 4; i++) {
            __asm__ volatile(".arch_extension crc\n\tcrc32b %w0, %w0, %w1"
                             : "+r"(hard) : "r"((uint32_t)data[i]));
            soft = crc32_soft(soft, data[i]);
        }
        check("crc32b", hard == soft);
    }
    if (caps & AROS_HWCAP_PMULL) {
        // 0b11 x 0b11 = 0b101 in carry-less arithmetic.
        uint64_t lo;
        __asm__ volatile(".arch_extension aes\n\t"
                         "movi v0.2d, #0\n\tmov x9, #3\n\tins v0.d[0], x9\n\t"
                         "pmull v1.1q, v0.1d, v0.1d\n\tumov %0, v1.d[0]"
                         : "=r"(lo) :: "x9", "v0", "v1");
        check("pmull", lo == 5);
    }
    if (caps & AROS_HWCAP_ASIMDDP) {
        // Four lanes of 1*2 summed into each 32-bit lane: 8.
        uint32_t lane;
        __asm__ volatile(".arch_extension dotprod\n\t"
                         "movi v0.16b, #1\n\tmovi v1.16b, #2\n\tmovi v2.4s, #0\n\t"
                         "udot v2.4s, v0.16b, v1.16b\n\tumov %w0, v2.s[0]"
                         : "=r"(lane) :: "v0", "v1", "v2");
        check("udot", lane == 8);
    }

    printf("[HWCAPPROBE] result=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 20 : 0;
}
