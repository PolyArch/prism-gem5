/*
 * Copyright (c) 2007 The Hewlett-Packard Development Company
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Gabe Black
 */

#ifndef __ARCH_X86_ISATRAITS_HH__
#define __ARCH_X86_ISATRAITS_HH__

#include "arch/x86/types.hh"
#include "arch/x86/x86_traits.hh"
#include "base/types.hh"

namespace LittleEndianGuest {}

namespace X86ISA
{
    //This makes sure the little endian version of certain functions
    //are used.
    using namespace LittleEndianGuest;

    // X86 does not have a delay slot
#define ISA_HAS_DELAY_SLOT 0

    // X86 NOP (XCHG rAX, rAX)
    //XXX This needs to be set to an intermediate instruction struct
    //which encodes this instruction

    //4k. This value is not constant on x86.
    const int LogVMPageSize = 12;
    const int VMPageSize = (1 << LogVMPageSize);

    const int PageShift = 12;
    const int PageBytes = 1ULL << PageShift;

    const int BranchPredAddrShiftAmt = 0;

    // Memory accesses can be unaligned
    const bool HasUnalignedMemAcc = true;

    const bool CurThreadInfoImplemented = false;
    const int CurThreadInfoReg = -1;

    const ExtMachInst NoopMachInst = {
        0x0,                            // No legacy prefixes.
        0x0,                            // No rex prefix.
        { 1, 0x0, 0x0, 0x90 },          // One opcode byte, 0x90.
        0x0, 0x0,                       // No modrm or sib.
        0, 0,                           // No immediate or displacement.
        8, 8, 8,                        // All sizes are 8.
        0,                              // Displacement size is 0.
        0,                              // DyInstTy = 0
        SixtyFourBitMode                // Behave as if we're in 64 bit
                                        // mode (this doesn't actually matter).
    };
}

#endif // __ARCH_X86_ISATRAITS_HH__
