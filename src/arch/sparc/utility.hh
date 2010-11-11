/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
 * All rights reserved.
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

#ifndef __ARCH_SPARC_UTILITY_HH__
#define __ARCH_SPARC_UTILITY_HH__

#include "arch/sparc/isa_traits.hh"
#include "arch/sparc/registers.hh"
#include "arch/sparc/tlb.hh"
#include "base/misc.hh"
#include "base/bitfield.hh"
#include "cpu/static_inst.hh"
#include "cpu/thread_context.hh"
#include "sim/fault.hh"

namespace SparcISA
{

inline PCState
buildRetPC(const PCState &curPC, const PCState &callPC)
{
    PCState ret = callPC;
    ret.uEnd();
    ret.pc(curPC.npc());
    return ret;
}

uint64_t getArgument(ThreadContext *tc, int &number, uint16_t size, bool fp);

static inline bool
inUserMode(ThreadContext *tc)
{
    return !((tc->readMiscRegNoEffect(MISCREG_PSTATE) & (1 << 2)) ||
             (tc->readMiscRegNoEffect(MISCREG_HPSTATE) & (1 << 2)));
}

/**
 * Function to insure ISA semantics about 0 registers.
 * @param tc The thread context.
 */
template <class TC>
void zeroRegisters(TC *tc);

void initCPU(ThreadContext *tc, int cpuId);

inline void
startupCPU(ThreadContext *tc, int cpuId)
{
#if FULL_SYSTEM
    // Other CPUs will get activated by IPIs
    if (cpuId == 0)
        tc->activate(0);
#else
    tc->activate(0);
#endif
}

void copyRegs(ThreadContext *src, ThreadContext *dest);

void copyMiscRegs(ThreadContext *src, ThreadContext *dest);

void skipFunction(ThreadContext *tc);

inline void
advancePC(PCState &pc, const StaticInstPtr inst)
{
    inst->advancePC(pc);
}

} // namespace SparcISA

#endif
