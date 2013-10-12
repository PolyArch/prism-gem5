/*
 * Copyright (c) 2011-2012 ARM Limited
 * All rights reserved
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
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
 * Copyright (c) 2011 Regents of the University of California
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
 * Authors: Steve Reinhardt
 *          Nathan Binkert
 *          Rick Strong
 */

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

#include "arch/tlb.hh"
#include "base/loader/symtab.hh"
#include "base/callback.hh"
#include "base/cprintf.hh"
#include "base/misc.hh"
#include "base/output.hh"
#include "base/trace.hh"
#include "cpu/base.hh"
#include "cpu/checker/cpu.hh"
#include "cpu/cpuevent.hh"
#include "cpu/profile.hh"
#include "cpu/thread_context.hh"
#include "debug/SyscallVerbose.hh"
#include "params/BaseCPU.hh"
#include "sim/full_system.hh"
#include "sim/process.hh"
#include "sim/sim_events.hh"
#include "sim/sim_exit.hh"
#include "sim/system.hh"

// Hack
#include "sim/stat_control.hh"

using namespace std;

vector<BaseCPU *> BaseCPU::cpuList;

// This variable reflects the max number of threads in any CPU.  Be
// careful to only use it once all the CPUs that you care about have
// been initialized
int maxThreadsPerCPU = 1;

CPUProgressEvent::CPUProgressEvent(BaseCPU *_cpu, Tick ival)
    : Event(Event::Progress_Event_Pri), _interval(ival), lastNumInst(0),
      cpu(_cpu), _repeatEvent(true)
{
    if (_interval)
        cpu->schedule(this, curTick() + _interval);
}

void
CPUProgressEvent::process()
{
    Counter temp = cpu->totalOps();
#ifndef NDEBUG
    double ipc = double(temp - lastNumInst) / (_interval / cpu->clockPeriod());

    DPRINTFN("%s progress event, total committed:%i, progress insts committed: "
             "%lli, IPC: %0.8d\n", cpu->name(), temp, temp - lastNumInst,
             ipc);
    ipc = 0.0;
#else
    cprintf("%lli: %s progress event, total committed:%i, progress insts "
            "committed: %lli\n", curTick(), cpu->name(), temp,
            temp - lastNumInst);
#endif
    lastNumInst = temp;

    if (_repeatEvent)
        cpu->schedule(this, curTick() + _interval);
}

const char *
CPUProgressEvent::description() const
{
    return "CPU Progress";
}

BaseCPU::BaseCPU(Params *p, bool is_checker)
    : MemObject(p), instCnt(0), _cpuId(p->cpu_id),
      _instMasterId(p->system->getMasterId(name() + ".inst")),
      _dataMasterId(p->system->getMasterId(name() + ".data")),
      _taskId(ContextSwitchTaskId::Unknown), _pid(Request::invldPid),
      _switchedOut(p->switched_out), _cacheLineSize(p->system->cacheLineSize()),
      interrupts(p->interrupts), profileEvent(NULL),
      numThreads(p->numThreads), system(p->system),
      _cpg(0)
{
    // if Python did not provide a valid ID, do it here
    if (_cpuId == -1 ) {
        _cpuId = cpuList.size();
    }

    // add self to global list of CPUs
    cpuList.push_back(this);

    DPRINTF(SyscallVerbose, "Constructing CPU with id %d\n", _cpuId);

    if (numThreads > maxThreadsPerCPU)
        maxThreadsPerCPU = numThreads;

    // allocate per-thread instruction-based event queues
    comInstEventQueue = new EventQueue *[numThreads];
    for (ThreadID tid = 0; tid < numThreads; ++tid)
        comInstEventQueue[tid] =
            new EventQueue("instruction-based event queue");

    //
    // set up instruction-count-based termination events, if any
    //
    if (p->max_insts_any_thread != 0) {
        const char *cause = "a thread reached the max instruction count";
        for (ThreadID tid = 0; tid < numThreads; ++tid)
            scheduleInstStop(tid, p->max_insts_any_thread, cause);
    }

    // Set up instruction-count-based termination events for SimPoints
    // Typically, there are more than one action points.
    // Simulation.py is responsible to take the necessary actions upon
    // exitting the simulation loop.
    if (!p->simpoint_start_insts.empty()) {
        const char *cause = "simpoint starting point found";
        for (size_t i = 0; i < p->simpoint_start_insts.size(); ++i)
            scheduleInstStop(0, p->simpoint_start_insts[i], cause);
    }

    if (p->max_insts_all_threads != 0) {
        const char *cause = "all threads reached the max instruction count";

        // allocate & initialize shared downcounter: each event will
        // decrement this when triggered; simulation will terminate
        // when counter reaches 0
        int *counter = new int;
        *counter = numThreads;
        for (ThreadID tid = 0; tid < numThreads; ++tid) {
            Event *event = new CountedExitEvent(cause, *counter);
            comInstEventQueue[tid]->schedule(event, p->max_insts_all_threads);
        }
    }

    // allocate per-thread load-based event queues
    comLoadEventQueue = new EventQueue *[numThreads];
    for (ThreadID tid = 0; tid < numThreads; ++tid)
        comLoadEventQueue[tid] = new EventQueue("load-based event queue");

    //
    // set up instruction-count-based termination events, if any
    //
    if (p->max_loads_any_thread != 0) {
        const char *cause = "a thread reached the max load count";
        for (ThreadID tid = 0; tid < numThreads; ++tid)
            scheduleLoadStop(tid, p->max_loads_any_thread, cause);
    }

    if (p->max_loads_all_threads != 0) {
        const char *cause = "all threads reached the max load count";
        // allocate & initialize shared downcounter: each event will
        // decrement this when triggered; simulation will terminate
        // when counter reaches 0
        int *counter = new int;
        *counter = numThreads;
        for (ThreadID tid = 0; tid < numThreads; ++tid) {
            Event *event = new CountedExitEvent(cause, *counter);
            comLoadEventQueue[tid]->schedule(event, p->max_loads_all_threads);
        }
    }

    functionTracingEnabled = false;
    if (p->function_trace) {
        const string fname = csprintf("ftrace.%s", name());
        functionTraceStream = simout.find(fname);
        if (!functionTraceStream)
            functionTraceStream = simout.create(fname);

        currentFunctionStart = currentFunctionEnd = 0;
        functionEntryTick = p->function_trace_start;

        if (p->function_trace_start == 0) {
            functionTracingEnabled = true;
        } else {
            typedef EventWrapper<BaseCPU, &BaseCPU::enableFunctionTrace> wrap;
            Event *event = new wrap(this, true);
            schedule(event, p->function_trace_start);
        }
    }

    // The interrupts should always be present unless this CPU is
    // switched in later or in case it is a checker CPU
    if (!params()->switched_out && !is_checker) {
        if (interrupts) {
            interrupts->setCPU(this);
        } else {
            fatal("CPU %s has no interrupt controller.\n"
                  "Ensure createInterruptController() is called.\n", name());
        }
    }

    if (FullSystem) {
        if (params()->profile)
            profileEvent = new ProfileEvent(this, params()->profile);
    }
    tracer = params()->tracer;

    if (params()->isa.size() != numThreads) {
        fatal("Number of ISAs (%i) assigned to the CPU does not equal number "
              "of threads (%i).\n", params()->isa.size(), numThreads);
    }
    _cpg = new CP_Graph(this);
}

void
BaseCPU::enableFunctionTrace()
{
    functionTracingEnabled = true;
}

BaseCPU::~BaseCPU()
{
    delete profileEvent;
    delete[] comLoadEventQueue;
    delete[] comInstEventQueue;
}

void
BaseCPU::init()
{
    if (!params()->switched_out) {
        registerThreadContexts();

        verifyMemoryMode();
    }
}

void
BaseCPU::startup()
{
    if (FullSystem) {
        if (!params()->switched_out && profileEvent)
            schedule(profileEvent, curTick());
    }

    if (params()->progress_interval) {
        new CPUProgressEvent(this, params()->progress_interval);
    }
}


void
BaseCPU::regStats()
{
    using namespace Stats;

    numCycles
        .name(name() + ".numCycles")
        .desc("number of cpu cycles simulated")
        ;

    numWorkItemsStarted
        .name(name() + ".numWorkItemsStarted")
        .desc("number of work items this cpu started")
        ;

    numWorkItemsCompleted
        .name(name() + ".numWorkItemsCompleted")
        .desc("number of work items this cpu completed")
        ;

    int size = threadContexts.size();
    if (size > 1) {
        for (int i = 0; i < size; ++i) {
            stringstream namestr;
            ccprintf(namestr, "%s.ctx%d", name(), i);
            threadContexts[i]->regStats(namestr.str());
        }
    } else if (size == 1)
        threadContexts[0]->regStats(name());
}

BaseMasterPort &
BaseCPU::getMasterPort(const string &if_name, PortID idx)
{
    // Get the right port based on name. This applies to all the
    // subclasses of the base CPU and relies on their implementation
    // of getDataPort and getInstPort. In all cases there methods
    // return a MasterPort pointer.
    if (if_name == "dcache_port")
        return getDataPort();
    else if (if_name == "icache_port")
        return getInstPort();
    else
        return MemObject::getMasterPort(if_name, idx);
}

void
BaseCPU::registerThreadContexts()
{
    ThreadID size = threadContexts.size();
    for (ThreadID tid = 0; tid < size; ++tid) {
        ThreadContext *tc = threadContexts[tid];

        /** This is so that contextId and cpuId match where there is a
         * 1cpu:1context relationship.  Otherwise, the order of registration
         * could affect the assignment and cpu 1 could have context id 3, for
         * example.  We may even want to do something like this for SMT so that
         * cpu 0 has the lowest thread contexts and cpu N has the highest, but
         * I'll just do this for now
         */
        if (numThreads == 1)
            tc->setContextId(system->registerThreadContext(tc, _cpuId));
        else
            tc->setContextId(system->registerThreadContext(tc));

        if (!FullSystem)
            tc->getProcessPtr()->assignThreadContext(tc->contextId());
    }
}


int
BaseCPU::findContext(ThreadContext *tc)
{
    ThreadID size = threadContexts.size();
    for (ThreadID tid = 0; tid < size; ++tid) {
        if (tc == threadContexts[tid])
            return tid;
    }
    return 0;
}

void
BaseCPU::switchOut()
{
    assert(!_switchedOut);
    _switchedOut = true;
    if (profileEvent && profileEvent->scheduled())
        deschedule(profileEvent);

    // Flush all TLBs in the CPU to avoid having stale translations if
    // it gets switched in later.
    flushTLBs();
}

void
BaseCPU::takeOverFrom(BaseCPU *oldCPU)
{
    assert(threadContexts.size() == oldCPU->threadContexts.size());
    assert(_cpuId == oldCPU->cpuId());
    assert(_switchedOut);
    assert(oldCPU != this);
    _pid = oldCPU->getPid();
    _taskId = oldCPU->taskId();
    _switchedOut = false;

    oldCPU->getCPG()->dumpCallstack();

    ThreadID size = threadContexts.size();
    for (ThreadID i = 0; i < size; ++i) {
        ThreadContext *newTC = threadContexts[i];
        ThreadContext *oldTC = oldCPU->threadContexts[i];

        newTC->takeOverFrom(oldTC);

        CpuEvent::replaceThreadContext(oldTC, newTC);

        assert(newTC->contextId() == oldTC->contextId());
        assert(newTC->threadId() == oldTC->threadId());
        system->replaceThreadContext(newTC, newTC->contextId());

        /* This code no longer works since the zero register (e.g.,
         * r31 on Alpha) doesn't necessarily contain zero at this
         * point.
           if (DTRACE(Context))
            ThreadContext::compare(oldTC, newTC);
        */

        BaseMasterPort *old_itb_port = oldTC->getITBPtr()->getMasterPort();
        BaseMasterPort *old_dtb_port = oldTC->getDTBPtr()->getMasterPort();
        BaseMasterPort *new_itb_port = newTC->getITBPtr()->getMasterPort();
        BaseMasterPort *new_dtb_port = newTC->getDTBPtr()->getMasterPort();

        // Move over any table walker ports if they exist
        if (new_itb_port) {
            assert(!new_itb_port->isConnected());
            assert(old_itb_port);
            assert(old_itb_port->isConnected());
            BaseSlavePort &slavePort = old_itb_port->getSlavePort();
            old_itb_port->unbind();
            new_itb_port->bind(slavePort);
        }
        if (new_dtb_port) {
            assert(!new_dtb_port->isConnected());
            assert(old_dtb_port);
            assert(old_dtb_port->isConnected());
            BaseSlavePort &slavePort = old_dtb_port->getSlavePort();
            old_dtb_port->unbind();
            new_dtb_port->bind(slavePort);
        }

        // Checker whether or not we have to transfer CheckerCPU
        // objects over in the switch
        CheckerCPU *oldChecker = oldTC->getCheckerCpuPtr();
        CheckerCPU *newChecker = newTC->getCheckerCpuPtr();
        if (oldChecker && newChecker) {
            BaseMasterPort *old_checker_itb_port =
                oldChecker->getITBPtr()->getMasterPort();
            BaseMasterPort *old_checker_dtb_port =
                oldChecker->getDTBPtr()->getMasterPort();
            BaseMasterPort *new_checker_itb_port =
                newChecker->getITBPtr()->getMasterPort();
            BaseMasterPort *new_checker_dtb_port =
                newChecker->getDTBPtr()->getMasterPort();

            // Move over any table walker ports if they exist for checker
            if (new_checker_itb_port) {
                assert(!new_checker_itb_port->isConnected());
                assert(old_checker_itb_port);
                assert(old_checker_itb_port->isConnected());
                BaseSlavePort &slavePort =
                    old_checker_itb_port->getSlavePort();
                old_checker_itb_port->unbind();
                new_checker_itb_port->bind(slavePort);
            }
            if (new_checker_dtb_port) {
                assert(!new_checker_dtb_port->isConnected());
                assert(old_checker_dtb_port);
                assert(old_checker_dtb_port->isConnected());
                BaseSlavePort &slavePort =
                    old_checker_dtb_port->getSlavePort();
                old_checker_dtb_port->unbind();
                new_checker_dtb_port->bind(slavePort);
            }
        }
    }

    interrupts = oldCPU->interrupts;
    interrupts->setCPU(this);
    oldCPU->interrupts = NULL;

    if (FullSystem) {
        for (ThreadID i = 0; i < size; ++i)
            threadContexts[i]->profileClear();

        if (profileEvent)
            schedule(profileEvent, curTick());
    }

    // All CPUs have an instruction and a data port, and the new CPU's
    // ports are dangling while the old CPU has its ports connected
    // already. Unbind the old CPU and then bind the ports of the one
    // we are switching to.
    assert(!getInstPort().isConnected());
    assert(oldCPU->getInstPort().isConnected());
    BaseSlavePort &inst_peer_port = oldCPU->getInstPort().getSlavePort();
    oldCPU->getInstPort().unbind();
    getInstPort().bind(inst_peer_port);

    assert(!getDataPort().isConnected());
    assert(oldCPU->getDataPort().isConnected());
    BaseSlavePort &data_peer_port = oldCPU->getDataPort().getSlavePort();
    oldCPU->getDataPort().unbind();
    getDataPort().bind(data_peer_port);
}

void
BaseCPU::flushTLBs()
{
    for (ThreadID i = 0; i < threadContexts.size(); ++i) {
        ThreadContext &tc(*threadContexts[i]);
        CheckerCPU *checker(tc.getCheckerCpuPtr());

        tc.getITBPtr()->flushAll();
        tc.getDTBPtr()->flushAll();
        if (checker) {
            checker->getITBPtr()->flushAll();
            checker->getDTBPtr()->flushAll();
        }
    }
}


BaseCPU::ProfileEvent::ProfileEvent(BaseCPU *_cpu, Tick _interval)
    : cpu(_cpu), interval(_interval)
{ }

void
BaseCPU::ProfileEvent::process()
{
    ThreadID size = cpu->threadContexts.size();
    for (ThreadID i = 0; i < size; ++i) {
        ThreadContext *tc = cpu->threadContexts[i];
        tc->profileSample();
    }

    cpu->schedule(this, curTick() + interval);
}

void
BaseCPU::serialize(std::ostream &os)
{
    SERIALIZE_SCALAR(instCnt);

    if (!_switchedOut) {
        /* Unlike _pid, _taskId is not serialized, as they are dynamically
         * assigned unique ids that are only meaningful for the duration of
         * a specific run. We will need to serialize the entire taskMap in
         * system. */
        SERIALIZE_SCALAR(_pid);

        interrupts->serialize(os);

        // Serialize the threads, this is done by the CPU implementation.
        for (ThreadID i = 0; i < numThreads; ++i) {
            nameOut(os, csprintf("%s.xc.%i", name(), i));
            serializeThread(os, i);
        }
    }
}

void
BaseCPU::unserialize(Checkpoint *cp, const std::string &section)
{
    UNSERIALIZE_SCALAR(instCnt);

    if (!_switchedOut) {
        UNSERIALIZE_SCALAR(_pid);
        interrupts->unserialize(cp, section);

        // Unserialize the threads, this is done by the CPU implementation.
        for (ThreadID i = 0; i < numThreads; ++i)
            unserializeThread(cp, csprintf("%s.xc.%i", section, i), i);
    }
}

void
BaseCPU::scheduleInstStop(ThreadID tid, Counter insts, const char *cause)
{
    const Tick now(comInstEventQueue[tid]->getCurTick());
    Event *event(new SimLoopExitEvent(cause, 0));

    comInstEventQueue[tid]->schedule(event, now + insts);
}

void
BaseCPU::scheduleLoadStop(ThreadID tid, Counter loads, const char *cause)
{
    const Tick now(comLoadEventQueue[tid]->getCurTick());
    Event *event(new SimLoopExitEvent(cause, 0));

    comLoadEventQueue[tid]->schedule(event, now + loads);
}


void
BaseCPU::traceFunctionsInternal(Addr pc)
{
    if (!debugSymbolTable)
        return;

    // if pc enters different function, print new function symbol and
    // update saved range.  Otherwise do nothing.
    if (pc < currentFunctionStart || pc >= currentFunctionEnd) {
        string sym_str;
        bool found = debugSymbolTable->findNearestSymbol(pc, sym_str,
                                                         currentFunctionStart,
                                                         currentFunctionEnd);

        if (!found) {
            // no symbol found: use addr as label
            sym_str = csprintf("0x%x", pc);
            currentFunctionStart = pc;
            currentFunctionEnd = pc + 1;
        }

        ccprintf(*functionTraceStream, " (%d)\n%d: %s",
                 curTick() - functionEntryTick, curTick(), sym_str);
        functionEntryTick = curTick();
    }
}

InstProfiler InstProfiler::Instance;

void
InstProfiler::init()
{
  profile_outfile = getenv("PROFILE_GEM5");
  enable_profile = (profile_outfile != 0);

  clear();

  min_addr.first = (Addr)-1;
  min_addr.second = (MicroPC) -1;

  if (enable_profile)
    registerExitCallback(new MakeCallback<InstProfiler,
                         &InstProfiler::printProfile>(this, true));
}

void
InstProfiler::profileAddr(Addr pc, MicroPC upc, StaticInstPtr curStaticInst)
{
  if (!enable_profile)
    return;

  // increment number of time pc executed.
  PC_UPC_t pc_upc = make_pair(pc, upc);
  exec_prof[pc_upc] ++;
  if (is_a_branch_target) {
    assert(last_branch_addr.first != 0);
    ++ branch_to_prof[last_branch_addr][pc_upc];
    ++ branch_from_prof[pc_upc][last_branch_addr];
    last_branch_addr.first = 0;
    last_branch_addr.second = 0;
    is_a_branch_target = false;
  }
  bool isControl = curStaticInst->isControl();
  bool isCall = curStaticInst->isCall();
  bool isReturn = curStaticInst->isReturn();
  bool isLoad = curStaticInst->isLoad();
  bool isStore = curStaticInst->isStore();

  if (isControl && !(isCall || isReturn)) {
    is_a_branch_target = true;
    last_branch_addr = pc_upc;
  }

  if (min_addr.first > pc_upc.first)
    min_addr = pc_upc;
  if (!disasm.count(pc_upc)) {
    disasm[pc_upc] =  curStaticInst->disassemble(pc_upc.first,
                                                 debugSymbolTable);
  }
  type_prof[pc_upc] = (
                       (isControl << 0)
                       | (isCall << 1)
                       | (isReturn << 2)
                       | (isLoad << 3)
                       | (isStore << 4));
}

void InstProfiler::printProfile(void)
{
  if (!enable_profile)
    return;

  uint64_t total_num_inst = 0;
  std::map<Addr, uint64_t> excProfPerSymbol;
  const char *profile_file_name = getenv("PROFILE_GEM5");
  assert(profile_file_name);
  std::fstream fout(profile_file_name, std::fstream::out);
  if (!fout.is_open()) {
    std::cerr << "Cannot open " << profile_file_name << "\n";
    return;
  }
  std::string disasmfile = std::string(profile_file_name) + ".disasm";
  std::fstream disasmout(disasmfile.c_str(), std::fstream::out);
  if (!disasmout.is_open()) {
    std::cerr << "Cannot open " << disasmfile << "\n";
    return;
  }

  std::map<PC_UPC_t, std::string>::iterator
    cur_it = disasm.find(min_addr);

  std::string sym_str = "";
  Addr currentFnStart = 0, currentFnEnd = 0;
  while (cur_it != disasm.end()) {
    if (debugSymbolTable) {
      if (cur_it->first.first < currentFnStart
          || cur_it->first.first >= currentFnEnd) {

        debugSymbolTable->findNearestSymbol(cur_it->first.first,
                                            sym_str,
                                            currentFnStart,
                                            currentFnEnd);
      }
    }
    if (branch_from_prof.count(cur_it->first)) {
      fout << "\n\t\t# Branches from ";
      for (std::map<PC_UPC_t, uint64_t>::iterator
             I = branch_from_prof[cur_it->first].begin(),
             E = branch_from_prof[cur_it->first].end();
           I != E; ++I) {
        fout << std::hex << I->first.first << "("
             << std::dec <<  I->second << ") ";
      }
      fout << "\n";
    }
    disasmout << cur_it->first.first << " " << cur_it->first.second << " "
              << sym_str << "+" << (cur_it->first.first - currentFnStart)
              << cur_it->second << " ";
    int ty = type_prof[cur_it->first];
    disasmout << ((ty & 0x1) ?"C": "_")
              << ((ty & 0x2) ?"A": "_")
              << ((ty & 0x4) ?"R": "_")
              << ((ty & 0x8) ?"L": "_")
              << ((ty & 0x10)?"S": "_") << "\n";

    fout << std::dec << std::setw(10) << exec_prof[cur_it->first]
         << " : "
         << std::hex
         << std::setw(10)<< cur_it->first.first
         << ","
         << std::setw(2) << cur_it->first.second
         << " : "
         << std::setw(30) << sym_str
         << "+"
         << std::setw(5) << (cur_it->first.first - currentFnStart)
         << "  "
         << cur_it->second << "\n";
    if (branch_to_prof.count(cur_it->first)) {
      fout << "\t\t# Branches To ";
      for (std::map<PC_UPC_t, uint64_t>::iterator
             I = branch_to_prof[cur_it->first].begin(),
             E = branch_to_prof[cur_it->first].end();
           I != E; ++I) {
        fout << I->first.first << "(" << I->second << ") ";
      }
      fout << "\n";
    }

    total_num_inst += exec_prof[cur_it->first];
    excProfPerSymbol[currentFnStart] += exec_prof[cur_it->first];
    cur_it = disasm.upper_bound(cur_it->first);
  }

  std::vector<std::pair<Addr, uint64_t> > sortedSymbolAddr;
  for (std::map<Addr, uint64_t>::iterator I = excProfPerSymbol.begin(),
         E = excProfPerSymbol.end(); I != E; ++I) {
    std::vector<std::pair<Addr, uint64_t> >::iterator insertAt = sortedSymbolAddr.begin();
    while (insertAt != sortedSymbolAddr.end()
           && insertAt->second < I->second ) {
      ++insertAt;
    }
    sortedSymbolAddr.insert(insertAt, *I);
  }

  fout << "Function Profile::\n";
  fout << "Total...: " << std::dec << total_num_inst << "\n";

  for (std::vector<std::pair<Addr,uint64_t> >::reverse_iterator
         I = sortedSymbolAddr.rbegin(),
         E = sortedSymbolAddr.rend(); I != E; ++I) {
    std::string sym = "";
    double perc = 100.0 * (double)I->second/(double)total_num_inst;
    debugSymbolTable->findSymbol(I->first, sym);
    fout << std::setprecision(2) << std::setw(5) << perc
         << " "
         << std::dec << std::setw(10) << I->second
         << "::"
         << std::hex << std::setw(10) << I->first
         << "  "
         << sym << "\n";
    if (perc < 1.0)
      break;
  }

  fout.close();
  disasmout.close();
}

