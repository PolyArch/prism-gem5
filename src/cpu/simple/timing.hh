/*
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
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
 */

#ifndef __CPU_SIMPLE_TIMING_HH__
#define __CPU_SIMPLE_TIMING_HH__

#include "cpu/simple/base.hh"

#include "params/TimingSimpleCPU.hh"

class TimingSimpleCPU : public BaseSimpleCPU
{
  public:

    TimingSimpleCPU(TimingSimpleCPUParams * params);
    virtual ~TimingSimpleCPU();

    virtual void init();

  public:
    Event *drainEvent;

  private:

    /*
     * If an access needs to be broken into fragments, currently at most two,
     * the the following two classes are used as the sender state of the
     * packets so the CPU can keep track of everything. In the main packet
     * sender state, there's an array with a spot for each fragment. If a
     * fragment has already been accepted by the CPU, aka isn't waiting for
     * a retry, it's pointer is NULL. After each fragment has successfully
     * been processed, the "outstanding" counter is decremented. Once the
     * count is zero, the entire larger access is complete.
     */
    class SplitMainSenderState : public Packet::SenderState
    {
      public:
        int outstanding;
        PacketPtr fragments[2];

        int
        getPendingFragment()
        {
            if (fragments[0]) {
                return 0;
            } else if (fragments[1]) {
                return 1;
            } else {
                return -1;
            }
        }
    };

    class SplitFragmentSenderState : public Packet::SenderState
    {
      public:
        SplitFragmentSenderState(PacketPtr _bigPkt, int _index) :
            bigPkt(_bigPkt), index(_index)
        {}
        PacketPtr bigPkt;
        int index;

        void
        clearFromParent()
        {
            SplitMainSenderState * main_send_state =
                dynamic_cast<SplitMainSenderState *>(bigPkt->senderState);
            main_send_state->fragments[index] = NULL;
        }
    };

    class FetchTranslation : public BaseTLB::Translation
    {
      protected:
        TimingSimpleCPU *cpu;

      public:
        FetchTranslation(TimingSimpleCPU *_cpu) : cpu(_cpu)
        {}

        void finish(Fault fault, RequestPtr req,
                ThreadContext *tc, bool write)
        {
            cpu->sendFetch(fault, req, tc);
        }
    };
    FetchTranslation fetchTranslation;

    class DataTranslation : public BaseTLB::Translation
    {
      protected:
        TimingSimpleCPU *cpu;
        uint8_t *data;
        uint64_t *res;
        bool read;

      public:
        DataTranslation(TimingSimpleCPU *_cpu,
                uint8_t *_data, uint64_t *_res, bool _read) :
            cpu(_cpu), data(_data), res(_res), read(_read)
        {}

        void
        finish(Fault fault, RequestPtr req,
                ThreadContext *tc, bool write)
        {
            cpu->sendData(fault, req, data, res, read);
            delete this;
        }
    };

    class SplitDataTranslation : public BaseTLB::Translation
    {
      public:
        struct WholeTranslationState
        {
          public:
            int outstanding;
            RequestPtr requests[2];
            RequestPtr mainReq;
            Fault faults[2];
            uint8_t *data;
            bool read;

            WholeTranslationState(RequestPtr req1, RequestPtr req2,
                    RequestPtr main, uint8_t *_data, bool _read)
            {
                outstanding = 2;
                requests[0] = req1;
                requests[1] = req2;
                mainReq = main;
                faults[0] = faults[1] = NoFault;
                data = _data;
                read = _read;
            }
        };

        TimingSimpleCPU *cpu;
        int index;
        WholeTranslationState *state;

        SplitDataTranslation(TimingSimpleCPU *_cpu, int _index,
                WholeTranslationState *_state) :
            cpu(_cpu), index(_index), state(_state)
        {}

        void
        finish(Fault fault, RequestPtr req,
                ThreadContext *tc, bool write)
        {
            assert(state);
            assert(state->outstanding);
            state->faults[index] = fault;
            if (--state->outstanding == 0) {
                cpu->sendSplitData(state->faults[0],
                                   state->faults[1],
                                   state->requests[0],
                                   state->requests[1],
                                   state->mainReq,
                                   state->data,
                                   state->read);
                delete state;
            }
            delete this;
        }
    };

    void sendData(Fault fault, RequestPtr req,
            uint8_t *data, uint64_t *res, bool read);
    void sendSplitData(Fault fault1, Fault fault2,
            RequestPtr req1, RequestPtr req2, RequestPtr req,
            uint8_t *data, bool read);

    void translationFault(Fault fault);

    void buildPacket(PacketPtr &pkt, RequestPtr req, bool read);
    void buildSplitPacket(PacketPtr &pkt1, PacketPtr &pkt2,
            RequestPtr req1, RequestPtr req2, RequestPtr req,
            uint8_t *data, bool read);

    bool handleReadPacket(PacketPtr pkt);
    // This function always implicitly uses dcache_pkt.
    bool handleWritePacket();

    class CpuPort : public Port
    {
      protected:
        TimingSimpleCPU *cpu;
        Tick lat;

      public:

        CpuPort(const std::string &_name, TimingSimpleCPU *_cpu, Tick _lat)
            : Port(_name, _cpu), cpu(_cpu), lat(_lat)
        { }

        bool snoopRangeSent;

      protected:

        virtual Tick recvAtomic(PacketPtr pkt);

        virtual void recvFunctional(PacketPtr pkt);

        virtual void recvStatusChange(Status status);

        virtual void getDeviceAddressRanges(AddrRangeList &resp,
                                            bool &snoop)
        { resp.clear(); snoop = false; }

        struct TickEvent : public Event
        {
            PacketPtr pkt;
            TimingSimpleCPU *cpu;

            TickEvent(TimingSimpleCPU *_cpu) : cpu(_cpu) {}
            const char *description() const { return "Timing CPU tick"; }
            void schedule(PacketPtr _pkt, Tick t);
        };

    };

    class IcachePort : public CpuPort
    {
      public:

        IcachePort(TimingSimpleCPU *_cpu, Tick _lat)
            : CpuPort(_cpu->name() + "-iport", _cpu, _lat), tickEvent(_cpu)
        { }

      protected:

        virtual bool recvTiming(PacketPtr pkt);

        virtual void recvRetry();

        struct ITickEvent : public TickEvent
        {

            ITickEvent(TimingSimpleCPU *_cpu)
                : TickEvent(_cpu) {}
            void process();
            const char *description() const { return "Timing CPU icache tick"; }
        };

        ITickEvent tickEvent;

    };

    class DcachePort : public CpuPort
    {
      public:

        DcachePort(TimingSimpleCPU *_cpu, Tick _lat)
            : CpuPort(_cpu->name() + "-dport", _cpu, _lat), tickEvent(_cpu)
        { }

        virtual void setPeer(Port *port);

      protected:

        virtual bool recvTiming(PacketPtr pkt);

        virtual void recvRetry();

        struct DTickEvent : public TickEvent
        {
            DTickEvent(TimingSimpleCPU *_cpu)
                : TickEvent(_cpu) {}
            void process();
            const char *description() const { return "Timing CPU dcache tick"; }
        };

        DTickEvent tickEvent;

    };

    IcachePort icachePort;
    DcachePort dcachePort;

    PacketPtr ifetch_pkt;
    PacketPtr dcache_pkt;

    Tick previousTick;

  public:

    virtual Port *getPort(const std::string &if_name, int idx = -1);

    virtual void serialize(std::ostream &os);
    virtual void unserialize(Checkpoint *cp, const std::string &section);

    virtual unsigned int drain(Event *drain_event);
    virtual void resume();

    void switchOut();
    void takeOverFrom(BaseCPU *oldCPU);

    virtual void activateContext(int thread_num, int delay);
    virtual void suspendContext(int thread_num);

    template <class T>
    Fault read(Addr addr, T &data, unsigned flags);

    template <class T>
    Fault write(T data, Addr addr, unsigned flags, uint64_t *res);

    void fetch();
    void sendFetch(Fault fault, RequestPtr req, ThreadContext *tc);
    void completeIfetch(PacketPtr );
    void completeDataAccess(PacketPtr pkt);
    void advanceInst(Fault fault);

    /**
     * Print state of address in memory system via PrintReq (for
     * debugging).
     */
    void printAddr(Addr a);

  private:

    typedef EventWrapper<TimingSimpleCPU, &TimingSimpleCPU::fetch> FetchEvent;
    FetchEvent fetchEvent;

    struct IprEvent : Event {
        Packet *pkt;
        TimingSimpleCPU *cpu;
        IprEvent(Packet *_pkt, TimingSimpleCPU *_cpu, Tick t);
        virtual void process();
        virtual const char *description() const;
    };

    void completeDrain();
};

#endif // __CPU_SIMPLE_TIMING_HH__
