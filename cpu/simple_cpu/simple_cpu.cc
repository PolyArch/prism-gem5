/*
 * Copyright (c) 2002-2004 The Regents of The University of Michigan
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
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <list>
#include <sstream>
#include <string>

#include "base/cprintf.hh"
#include "base/inifile.hh"
#include "base/loader/symtab.hh"
#include "base/misc.hh"
#include "base/pollevent.hh"
#include "base/range.hh"
#include "base/trace.hh"
#include "base/stats/events.hh"
#include "cpu/base_cpu.hh"
#include "cpu/exec_context.hh"
#include "cpu/exetrace.hh"
#include "cpu/full_cpu/smt.hh"
#include "cpu/sampling_cpu/sampling_cpu.hh"
#include "cpu/simple_cpu/simple_cpu.hh"
#include "cpu/static_inst.hh"
#include "mem/base_mem.hh"
#include "mem/mem_interface.hh"
#include "sim/builder.hh"
#include "sim/debug.hh"
#include "sim/host.hh"
#include "sim/sim_events.hh"
#include "sim/sim_object.hh"
#include "sim/stats.hh"

#ifdef FULL_SYSTEM
#include "base/remote_gdb.hh"
#include "dev/alpha_access.h"
#include "dev/pciareg.h"
#include "mem/functional_mem/memory_control.hh"
#include "mem/functional_mem/physical_memory.hh"
#include "sim/system.hh"
#include "targetarch/alpha_memory.hh"
#include "targetarch/vtophys.hh"
#else // !FULL_SYSTEM
#include "eio/eio.hh"
#include "mem/functional_mem/functional_memory.hh"
#endif // FULL_SYSTEM

using namespace std;


SimpleCPU::TickEvent::TickEvent(SimpleCPU *c, int w)
    : Event(&mainEventQueue, CPU_Tick_Pri), cpu(c), width(w)
{
}

void
SimpleCPU::TickEvent::process()
{
    int count = width;
    do {
        cpu->tick();
    } while (--count > 0 && cpu->status() == Running);
}

const char *
SimpleCPU::TickEvent::description()
{
    return "SimpleCPU tick event";
}


SimpleCPU::CacheCompletionEvent::CacheCompletionEvent(SimpleCPU *_cpu)
    : Event(&mainEventQueue), cpu(_cpu)
{
}

void SimpleCPU::CacheCompletionEvent::process()
{
    cpu->processCacheCompletion();
}

const char *
SimpleCPU::CacheCompletionEvent::description()
{
    return "SimpleCPU cache completion event";
}

#ifdef FULL_SYSTEM
SimpleCPU::SimpleCPU(const string &_name,
                     System *_system,
                     Counter max_insts_any_thread,
                     Counter max_insts_all_threads,
                     Counter max_loads_any_thread,
                     Counter max_loads_all_threads,
                     AlphaITB *itb, AlphaDTB *dtb,
                     FunctionalMemory *mem,
                     MemInterface *icache_interface,
                     MemInterface *dcache_interface,
                     bool _def_reg, Tick freq,
                     bool _function_trace, Tick _function_trace_start,
                     int width)
    : BaseCPU(_name, /* number_of_threads */ 1, _def_reg,
              max_insts_any_thread, max_insts_all_threads,
              max_loads_any_thread, max_loads_all_threads,
              _system, freq, _function_trace, _function_trace_start),
#else
SimpleCPU::SimpleCPU(const string &_name, Process *_process,
                     Counter max_insts_any_thread,
                     Counter max_insts_all_threads,
                     Counter max_loads_any_thread,
                     Counter max_loads_all_threads,
                     MemInterface *icache_interface,
                     MemInterface *dcache_interface,
                     bool _def_reg,
                     bool _function_trace, Tick _function_trace_start,
                     int width)
    : BaseCPU(_name, /* number_of_threads */ 1, _def_reg,
              max_insts_any_thread, max_insts_all_threads,
              max_loads_any_thread, max_loads_all_threads,
              _function_trace, _function_trace_start),
#endif
      tickEvent(this, width), xc(NULL), cacheCompletionEvent(this)
{
    _status = Idle;
#ifdef FULL_SYSTEM
    xc = new ExecContext(this, 0, system, itb, dtb, mem);

    // initialize CPU, including PC
    TheISA::initCPU(&xc->regs);
#else
    xc = new ExecContext(this, /* thread_num */ 0, _process, /* asid */ 0);
#endif // !FULL_SYSTEM

    icacheInterface = icache_interface;
    dcacheInterface = dcache_interface;

    memReq = new MemReq();
    memReq->xc = xc;
    memReq->asid = 0;
    memReq->data = new uint8_t[64];

    numInst = 0;
    startNumInst = 0;
    numLoad = 0;
    startNumLoad = 0;
    lastIcacheStall = 0;
    lastDcacheStall = 0;

    execContexts.push_back(xc);
}

SimpleCPU::~SimpleCPU()
{
}

void
SimpleCPU::switchOut(SamplingCPU *s)
{
    sampler = s;
    if (status() == DcacheMissStall) {
        DPRINTF(Sampler,"Outstanding dcache access, waiting for completion\n");
        _status = DcacheMissSwitch;
    }
    else {
        _status = SwitchedOut;

        if (tickEvent.scheduled())
            tickEvent.squash();

        sampler->signalSwitched();
    }
}


void
SimpleCPU::takeOverFrom(BaseCPU *oldCPU)
{
    BaseCPU::takeOverFrom(oldCPU);

    assert(!tickEvent.scheduled());

    // if any of this CPU's ExecContexts are active, mark the CPU as
    // running and schedule its tick event.
    for (int i = 0; i < execContexts.size(); ++i) {
        ExecContext *xc = execContexts[i];
        if (xc->status() == ExecContext::Active && _status != Running) {
            _status = Running;
            tickEvent.schedule(curTick);
        }
    }
}


void
SimpleCPU::activateContext(int thread_num, int delay)
{
    assert(thread_num == 0);
    assert(xc);

    assert(_status == Idle);
    notIdleFraction++;
    scheduleTickEvent(delay);
    _status = Running;
}


void
SimpleCPU::suspendContext(int thread_num)
{
    assert(thread_num == 0);
    assert(xc);

    assert(_status == Running);
    notIdleFraction--;
    unscheduleTickEvent();
    _status = Idle;
}


void
SimpleCPU::deallocateContext(int thread_num)
{
    // for now, these are equivalent
    suspendContext(thread_num);
}


void
SimpleCPU::haltContext(int thread_num)
{
    // for now, these are equivalent
    suspendContext(thread_num);
}


void
SimpleCPU::regStats()
{
    using namespace Stats;

    BaseCPU::regStats();

    numInsts
        .name(name() + ".num_insts")
        .desc("Number of instructions executed")
        ;

    numMemRefs
        .name(name() + ".num_refs")
        .desc("Number of memory references")
        ;

    notIdleFraction
        .name(name() + ".not_idle_fraction")
        .desc("Percentage of non-idle cycles")
        ;

    idleFraction
        .name(name() + ".idle_fraction")
        .desc("Percentage of idle cycles")
        ;

    icacheStallCycles
        .name(name() + ".icache_stall_cycles")
        .desc("ICache total stall cycles")
        .prereq(icacheStallCycles)
        ;

    dcacheStallCycles
        .name(name() + ".dcache_stall_cycles")
        .desc("DCache total stall cycles")
        .prereq(dcacheStallCycles)
        ;

    idleFraction = constant(1.0) - notIdleFraction;
}

void
SimpleCPU::resetStats()
{
    startNumInst = numInst;
    notIdleFraction = (_status != Idle);
}

void
SimpleCPU::serialize(ostream &os)
{
    BaseCPU::serialize(os);
    SERIALIZE_ENUM(_status);
    SERIALIZE_SCALAR(inst);
    nameOut(os, csprintf("%s.xc", name()));
    xc->serialize(os);
    nameOut(os, csprintf("%s.tickEvent", name()));
    tickEvent.serialize(os);
    nameOut(os, csprintf("%s.cacheCompletionEvent", name()));
    cacheCompletionEvent.serialize(os);
}

void
SimpleCPU::unserialize(Checkpoint *cp, const string &section)
{
    BaseCPU::unserialize(cp, section);
    UNSERIALIZE_ENUM(_status);
    UNSERIALIZE_SCALAR(inst);
    xc->unserialize(cp, csprintf("%s.xc", section));
    tickEvent.unserialize(cp, csprintf("%s.tickEvent", section));
    cacheCompletionEvent
        .unserialize(cp, csprintf("%s.cacheCompletionEvent", section));
}

void
change_thread_state(int thread_number, int activate, int priority)
{
}

Fault
SimpleCPU::copySrcTranslate(Addr src)
{
    static bool no_warn = true;
    int blk_size = (dcacheInterface) ? dcacheInterface->getBlockSize() : 64;
    // Only support block sizes of 64 atm.
    assert(blk_size == 64);
    int offset = src & (blk_size - 1);

    // Make sure block doesn't span page
    if (no_warn &&
        (src & TheISA::PageMask) != ((src + blk_size) & TheISA::PageMask) &&
        (src >> 40) != 0xfffffc) {
        warn("Copied block source spans pages %x.", src);
        no_warn = false;
    }

    memReq->reset(src & ~(blk_size - 1), blk_size);

    // translate to physical address
    Fault fault = xc->translateDataReadReq(memReq);

    assert(fault != Alignment_Fault);

    if (fault == No_Fault) {
        xc->copySrcAddr = src;
        xc->copySrcPhysAddr = memReq->paddr + offset;
    } else {
        xc->copySrcAddr = 0;
        xc->copySrcPhysAddr = 0;
    }
    return fault;
}

Fault
SimpleCPU::copy(Addr dest)
{
    static bool no_warn = true;
    int blk_size = (dcacheInterface) ? dcacheInterface->getBlockSize() : 64;
    // Only support block sizes of 64 atm.
    assert(blk_size == 64);
    uint8_t data[blk_size];
    //assert(xc->copySrcAddr);
    int offset = dest & (blk_size - 1);

    // Make sure block doesn't span page
    if (no_warn &&
        (dest & TheISA::PageMask) != ((dest + blk_size) & TheISA::PageMask) &&
        (dest >> 40) != 0xfffffc) {
        no_warn = false;
        warn("Copied block destination spans pages %x. ", dest);
    }

    memReq->reset(dest & ~(blk_size -1), blk_size);
    // translate to physical address
    Fault fault = xc->translateDataWriteReq(memReq);

    assert(fault != Alignment_Fault);

    if (fault == No_Fault) {
        Addr dest_addr = memReq->paddr + offset;
        // Need to read straight from memory since we have more than 8 bytes.
        memReq->paddr = xc->copySrcPhysAddr;
        xc->mem->read(memReq, data);
        memReq->paddr = dest_addr;
        xc->mem->write(memReq, data);
        if (dcacheInterface) {
            memReq->cmd = Copy;
            memReq->completionEvent = NULL;
            memReq->paddr = xc->copySrcPhysAddr;
            memReq->dest = dest_addr;
            memReq->size = 64;
            memReq->time = curTick;
            dcacheInterface->access(memReq);
        }
    }
    return fault;
}

// precise architected memory state accessor macros
template <class T>
Fault
SimpleCPU::read(Addr addr, T &data, unsigned flags)
{
    if (status() == DcacheMissStall) {
        Fault fault = xc->read(memReq,data);

        if (traceData) {
            traceData->setAddr(addr);
            if (fault == No_Fault)
                traceData->setData(data);
        }
        return fault;
    }

    memReq->reset(addr, sizeof(T), flags);

    // translate to physical address
    Fault fault = xc->translateDataReadReq(memReq);

    // if we have a cache, do cache access too
    if (fault == No_Fault && dcacheInterface) {
        memReq->cmd = Read;
        memReq->completionEvent = NULL;
        memReq->time = curTick;
        MemAccessResult result = dcacheInterface->access(memReq);

        // Ugly hack to get an event scheduled *only* if the access is
        // a miss.  We really should add first-class support for this
        // at some point.
        if (result != MA_HIT && dcacheInterface->doEvents()) {
            memReq->completionEvent = &cacheCompletionEvent;
            lastDcacheStall = curTick;
            unscheduleTickEvent();
            _status = DcacheMissStall;
        } else {
            // do functional access
            fault = xc->read(memReq, data);

            if (traceData) {
                traceData->setAddr(addr);
                if (fault == No_Fault)
                    traceData->setData(data);
            }
        }
    } else if(fault == No_Fault) {
        // do functional access
        fault = xc->read(memReq, data);

        if (traceData) {
            traceData->setAddr(addr);
            if (fault == No_Fault)
                traceData->setData(data);
        }
    }

    if (!dcacheInterface && (memReq->flags & UNCACHEABLE))
        recordEvent("Uncached Read");

    return fault;
}

#ifndef DOXYGEN_SHOULD_SKIP_THIS

template
Fault
SimpleCPU::read(Addr addr, uint64_t &data, unsigned flags);

template
Fault
SimpleCPU::read(Addr addr, uint32_t &data, unsigned flags);

template
Fault
SimpleCPU::read(Addr addr, uint16_t &data, unsigned flags);

template
Fault
SimpleCPU::read(Addr addr, uint8_t &data, unsigned flags);

#endif //DOXYGEN_SHOULD_SKIP_THIS

template<>
Fault
SimpleCPU::read(Addr addr, double &data, unsigned flags)
{
    return read(addr, *(uint64_t*)&data, flags);
}

template<>
Fault
SimpleCPU::read(Addr addr, float &data, unsigned flags)
{
    return read(addr, *(uint32_t*)&data, flags);
}


template<>
Fault
SimpleCPU::read(Addr addr, int32_t &data, unsigned flags)
{
    return read(addr, (uint32_t&)data, flags);
}


template <class T>
Fault
SimpleCPU::write(T data, Addr addr, unsigned flags, uint64_t *res)
{
    if (traceData) {
        traceData->setAddr(addr);
        traceData->setData(data);
    }

    memReq->reset(addr, sizeof(T), flags);

    // translate to physical address
    Fault fault = xc->translateDataWriteReq(memReq);

    // do functional access
    if (fault == No_Fault)
        fault = xc->write(memReq, data);

    if (fault == No_Fault && dcacheInterface) {
        memReq->cmd = Write;
        memcpy(memReq->data,(uint8_t *)&data,memReq->size);
        memReq->completionEvent = NULL;
        memReq->time = curTick;
        MemAccessResult result = dcacheInterface->access(memReq);

        // Ugly hack to get an event scheduled *only* if the access is
        // a miss.  We really should add first-class support for this
        // at some point.
        if (result != MA_HIT && dcacheInterface->doEvents()) {
            memReq->completionEvent = &cacheCompletionEvent;
            lastDcacheStall = curTick;
            unscheduleTickEvent();
            _status = DcacheMissStall;
        }
    }

    if (res && (fault == No_Fault))
        *res = memReq->result;

    if (!dcacheInterface && (memReq->flags & UNCACHEABLE))
        recordEvent("Uncached Write");

    return fault;
}


#ifndef DOXYGEN_SHOULD_SKIP_THIS
template
Fault
SimpleCPU::write(uint64_t data, Addr addr, unsigned flags, uint64_t *res);

template
Fault
SimpleCPU::write(uint32_t data, Addr addr, unsigned flags, uint64_t *res);

template
Fault
SimpleCPU::write(uint16_t data, Addr addr, unsigned flags, uint64_t *res);

template
Fault
SimpleCPU::write(uint8_t data, Addr addr, unsigned flags, uint64_t *res);

#endif //DOXYGEN_SHOULD_SKIP_THIS

template<>
Fault
SimpleCPU::write(double data, Addr addr, unsigned flags, uint64_t *res)
{
    return write(*(uint64_t*)&data, addr, flags, res);
}

template<>
Fault
SimpleCPU::write(float data, Addr addr, unsigned flags, uint64_t *res)
{
    return write(*(uint32_t*)&data, addr, flags, res);
}


template<>
Fault
SimpleCPU::write(int32_t data, Addr addr, unsigned flags, uint64_t *res)
{
    return write((uint32_t)data, addr, flags, res);
}


#ifdef FULL_SYSTEM
Addr
SimpleCPU::dbg_vtophys(Addr addr)
{
    return vtophys(xc, addr);
}
#endif // FULL_SYSTEM

Tick save_cycle = 0;


void
SimpleCPU::processCacheCompletion()
{
    switch (status()) {
      case IcacheMissStall:
        icacheStallCycles += curTick - lastIcacheStall;
        _status = IcacheMissComplete;
        scheduleTickEvent(1);
        break;
      case DcacheMissStall:
        if (memReq->cmd.isRead()) {
            curStaticInst->execute(this,traceData);
        }
        dcacheStallCycles += curTick - lastDcacheStall;
        _status = Running;
        scheduleTickEvent(1);
        break;
      case DcacheMissSwitch:
        if (memReq->cmd.isRead()) {
            curStaticInst->execute(this,traceData);
        }
        _status = SwitchedOut;
        sampler->signalSwitched();
      case SwitchedOut:
        // If this CPU has been switched out due to sampling/warm-up,
        // ignore any further status changes (e.g., due to cache
        // misses outstanding at the time of the switch).
        return;
      default:
        panic("SimpleCPU::processCacheCompletion: bad state");
        break;
    }
}

#ifdef FULL_SYSTEM
void
SimpleCPU::post_interrupt(int int_num, int index)
{
    BaseCPU::post_interrupt(int_num, index);

    if (xc->status() == ExecContext::Suspended) {
                DPRINTF(IPI,"Suspended Processor awoke\n");
        xc->activate();
    }
}
#endif // FULL_SYSTEM

/* start simulation, program loaded, processor precise state initialized */
void
SimpleCPU::tick()
{
    numCycles++;

    traceData = NULL;

    Fault fault = No_Fault;

#ifdef FULL_SYSTEM
    if (checkInterrupts && check_interrupts() && !xc->inPalMode() &&
        status() != IcacheMissComplete) {
        int ipl = 0;
        int summary = 0;
        checkInterrupts = false;
        IntReg *ipr = xc->regs.ipr;

        if (xc->regs.ipr[TheISA::IPR_SIRR]) {
            for (int i = TheISA::INTLEVEL_SOFTWARE_MIN;
                 i < TheISA::INTLEVEL_SOFTWARE_MAX; i++) {
                if (ipr[TheISA::IPR_SIRR] & (ULL(1) << i)) {
                    // See table 4-19 of 21164 hardware reference
                    ipl = (i - TheISA::INTLEVEL_SOFTWARE_MIN) + 1;
                    summary |= (ULL(1) << i);
                }
            }
        }

        uint64_t interrupts = xc->cpu->intr_status();
        for (int i = TheISA::INTLEVEL_EXTERNAL_MIN;
            i < TheISA::INTLEVEL_EXTERNAL_MAX; i++) {
            if (interrupts & (ULL(1) << i)) {
                // See table 4-19 of 21164 hardware reference
                ipl = i;
                summary |= (ULL(1) << i);
            }
        }

        if (ipr[TheISA::IPR_ASTRR])
            panic("asynchronous traps not implemented\n");

        if (ipl && ipl > xc->regs.ipr[TheISA::IPR_IPLR]) {
            ipr[TheISA::IPR_ISR] = summary;
            ipr[TheISA::IPR_INTID] = ipl;
            xc->ev5_trap(Interrupt_Fault);

            DPRINTF(Flow, "Interrupt! IPLR=%d ipl=%d summary=%x\n",
                    ipr[TheISA::IPR_IPLR], ipl, summary);
        }
    }
#endif

    // maintain $r0 semantics
    xc->regs.intRegFile[ZeroReg] = 0;
#ifdef TARGET_ALPHA
    xc->regs.floatRegFile.d[ZeroReg] = 0.0;
#endif // TARGET_ALPHA

    if (status() == IcacheMissComplete) {
        // We've already fetched an instruction and were stalled on an
        // I-cache miss.  No need to fetch it again.

        // Set status to running; tick event will get rescheduled if
        // necessary at end of tick() function.
        _status = Running;
    }
    else {
        // Try to fetch an instruction

        // set up memory request for instruction fetch
#ifdef FULL_SYSTEM
#define IFETCH_FLAGS(pc)	((pc) & 1) ? PHYSICAL : 0
#else
#define IFETCH_FLAGS(pc)	0
#endif

        memReq->cmd = Read;
        memReq->reset(xc->regs.pc & ~3, sizeof(uint32_t),
                     IFETCH_FLAGS(xc->regs.pc));

        fault = xc->translateInstReq(memReq);

        if (fault == No_Fault)
            fault = xc->mem->read(memReq, inst);

        if (icacheInterface && fault == No_Fault) {
            memReq->completionEvent = NULL;

            memReq->time = curTick;
            MemAccessResult result = icacheInterface->access(memReq);

            // Ugly hack to get an event scheduled *only* if the access is
            // a miss.  We really should add first-class support for this
            // at some point.
            if (result != MA_HIT && icacheInterface->doEvents()) {
                memReq->completionEvent = &cacheCompletionEvent;
                lastIcacheStall = curTick;
                unscheduleTickEvent();
                _status = IcacheMissStall;
                return;
            }
        }
    }

    // If we've got a valid instruction (i.e., no fault on instruction
    // fetch), then execute it.
    if (fault == No_Fault) {

        // keep an instruction count
        numInst++;
        numInsts++;

        // check for instruction-count-based events
        comInstEventQueue[0]->serviceEvents(numInst);

        // decode the instruction
        inst = htoa(inst);
        curStaticInst = StaticInst<TheISA>::decode(inst);

        traceData = Trace::getInstRecord(curTick, xc, this, curStaticInst,
                                         xc->regs.pc);

#ifdef FULL_SYSTEM
        xc->setInst(inst);
#endif // FULL_SYSTEM

        xc->func_exe_inst++;

        fault = curStaticInst->execute(this, traceData);

#ifdef FULL_SYSTEM
        if (xc->fnbin)
            xc->execute(curStaticInst.get());
#endif

        if (curStaticInst->isMemRef()) {
            numMemRefs++;
        }

        if (curStaticInst->isLoad()) {
            ++numLoad;
            comLoadEventQueue[0]->serviceEvents(numLoad);
        }

        if (traceData)
            traceData->finalize();

        traceFunctions(xc->regs.pc);

    }	// if (fault == No_Fault)

    if (fault != No_Fault) {
#ifdef FULL_SYSTEM
        xc->ev5_trap(fault);
#else // !FULL_SYSTEM
        fatal("fault (%d) detected @ PC 0x%08p", fault, xc->regs.pc);
#endif // FULL_SYSTEM
    }
    else {
        // go to the next instruction
        xc->regs.pc = xc->regs.npc;
        xc->regs.npc += sizeof(MachInst);
    }

#ifdef FULL_SYSTEM
    Addr oldpc;
    do {
        oldpc = xc->regs.pc;
        system->pcEventQueue.service(xc);
    } while (oldpc != xc->regs.pc);
#endif

    assert(status() == Running ||
           status() == Idle ||
           status() == DcacheMissStall);

    if (status() == Running && !tickEvent.scheduled())
        tickEvent.schedule(curTick + 1);
}


////////////////////////////////////////////////////////////////////////
//
//  SimpleCPU Simulation Object
//
BEGIN_DECLARE_SIM_OBJECT_PARAMS(SimpleCPU)

    Param<Counter> max_insts_any_thread;
    Param<Counter> max_insts_all_threads;
    Param<Counter> max_loads_any_thread;
    Param<Counter> max_loads_all_threads;

#ifdef FULL_SYSTEM
    SimObjectParam<AlphaITB *> itb;
    SimObjectParam<AlphaDTB *> dtb;
    SimObjectParam<FunctionalMemory *> mem;
    SimObjectParam<System *> system;
    Param<int> mult;
#else
    SimObjectParam<Process *> workload;
#endif // FULL_SYSTEM

    SimObjectParam<BaseMem *> icache;
    SimObjectParam<BaseMem *> dcache;

    Param<bool> defer_registration;
    Param<int> width;
    Param<bool> function_trace;
    Param<Tick> function_trace_start;

END_DECLARE_SIM_OBJECT_PARAMS(SimpleCPU)

BEGIN_INIT_SIM_OBJECT_PARAMS(SimpleCPU)

    INIT_PARAM_DFLT(max_insts_any_thread,
                    "terminate when any thread reaches this inst count",
                    0),
    INIT_PARAM_DFLT(max_insts_all_threads,
                    "terminate when all threads have reached this inst count",
                    0),
    INIT_PARAM_DFLT(max_loads_any_thread,
                    "terminate when any thread reaches this load count",
                    0),
    INIT_PARAM_DFLT(max_loads_all_threads,
                    "terminate when all threads have reached this load count",
                    0),

#ifdef FULL_SYSTEM
    INIT_PARAM(itb, "Instruction TLB"),
    INIT_PARAM(dtb, "Data TLB"),
    INIT_PARAM(mem, "memory"),
    INIT_PARAM(system, "system object"),
    INIT_PARAM_DFLT(mult, "system clock multiplier", 1),
#else
    INIT_PARAM(workload, "processes to run"),
#endif // FULL_SYSTEM

    INIT_PARAM_DFLT(icache, "L1 instruction cache object", NULL),
    INIT_PARAM_DFLT(dcache, "L1 data cache object", NULL),
    INIT_PARAM_DFLT(defer_registration, "defer registration with system "
                    "(for sampling)", false),

    INIT_PARAM_DFLT(width, "cpu width", 1),
    INIT_PARAM_DFLT(function_trace, "Enable function trace", false),
    INIT_PARAM_DFLT(function_trace_start, "Cycle to start function trace", 0)

END_INIT_SIM_OBJECT_PARAMS(SimpleCPU)


CREATE_SIM_OBJECT(SimpleCPU)
{
    SimpleCPU *cpu;
#ifdef FULL_SYSTEM
    if (mult != 1)
        panic("processor clock multiplier must be 1\n");

    cpu = new SimpleCPU(getInstanceName(), system,
                        max_insts_any_thread, max_insts_all_threads,
                        max_loads_any_thread, max_loads_all_threads,
                        itb, dtb, mem,
                        (icache) ? icache->getInterface() : NULL,
                        (dcache) ? dcache->getInterface() : NULL,
                        defer_registration,
                        ticksPerSecond * mult,
                        function_trace, function_trace_start,
                        width);
#else

    cpu = new SimpleCPU(getInstanceName(), workload,
                        max_insts_any_thread, max_insts_all_threads,
                        max_loads_any_thread, max_loads_all_threads,
                        (icache) ? icache->getInterface() : NULL,
                        (dcache) ? dcache->getInterface() : NULL,
                        defer_registration,
                        function_trace, function_trace_start,
                        width);

#endif // FULL_SYSTEM

    return cpu;
}

REGISTER_SIM_OBJECT("SimpleCPU", SimpleCPU)

