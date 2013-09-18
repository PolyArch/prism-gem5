
#ifndef CRITICAL_PATH_HH
#define CRITICAL_PATH_HH

#include <stdint.h>

#include <cassert>
#include <iostream>
#include <map>
#include <vector>

//#include "base/fast_alloc.hh"
#include "base/refcnt.hh"
#include "cpu/crtpath/crtpathnode.hh"
#include "cpu/op_class.hh"
#include "gzstream.hh"


class BaseCPU;

class CP_Node;
typedef RefCountingPtr<CP_Node> CP_NodePtr;
class CP_Graph;
typedef RefCountingPtr<CP_Graph> CP_GraphPtr;


class CP_Node: public RefCounted {
public:
  CP_Node(uint64_t s);
  ~CP_Node() {
    -- CP_Node::_count;
    CP_Node::_del_count ++;
  }

  static uint64_t _count;
  static uint64_t _total_count;
  static uint64_t _del_count;

  CP_NodePtr next, prev;
  CP_NodePtr prev_mem;
  CP_NodePtr next_mem;


  uint64_t seq;
  uint64_t index;
  uint64_t fetch_cycle;
  uint64_t icache_lat;
  uint64_t dispatch_cycle;
  uint64_t ready_cycle;
  uint64_t execute_cycle;
  uint64_t complete_cycle;
  uint64_t committed_cycle;
  uint64_t startwb_cycle;
  uint64_t donewb_cycle;

  bool ctrl_mispredict;
  bool spec_mispredict;
  bool squashed;
  bool isload, isstore;
  bool ctrl;
  bool call,ret;
  bool serialBefore, serialAfter, nonSpec, storeCond, prefetch;
  bool integer, floating, squashAfter,writeBar,memBar,syscall;

  uint64_t pc;
  uint16_t micropc;
  uint16_t opclass;
  bool kernelStart;
  bool kernelStop;
  uint64_t eff_addr, eff_addr2;

  uint8_t regfile_read, regfile_write;
  uint8_t regfile_fread, regfile_fwrite;
  uint8_t rob_read, rob_write;
  uint8_t iw_read, iw_write;
  uint8_t rename_read, rename_write;

  bool wrote_to_disk;

  std::map<uint64_t, bool> prod_seq;
  std::vector<CP_NodePtr> producers;
  CP_NodePtr mem_pred;
  CP_NodePtr cache_pred;

  void fetch(uint64_t cycle) {
    fetch_cycle = cycle;
  }
  void dispatch(uint64_t cycle) {
    assert(cycle >= fetch_cycle);
    dispatch_cycle = cycle;
  }
  void icache_latency(uint64_t cycle) {
    assert(icache_lat==0);
    icache_lat = cycle;
  }
  void ready(uint64_t cycle) {
    assert(cycle >= dispatch_cycle);
    ready_cycle = cycle;
  }
  void execute(uint64_t cycle) {
    assert(cycle >= execute_cycle);
    execute_cycle = cycle;
  }
  void complete(uint64_t cycle) {
    assert(cycle >= execute_cycle);
    complete_cycle = cycle;
  }
  void committed(uint64_t cycle) {
    assert(cycle >= complete_cycle);
    committed_cycle = cycle;
  }

  //Come back and give this tighter check
  void startWB(uint64_t cycle) {
    assert(cycle >= execute_cycle);
    startwb_cycle = cycle;
  }

  void doneWB(uint64_t cycle) {
    assert(cycle >= startwb_cycle);
    donewb_cycle = cycle;
  }

  void data_dep(CP_NodePtr prod) {
    producers.push_back(prod);
  }
  void ctrl_dep() {
    ctrl_mispredict = true;
  }
  void spec_dep() {
    spec_mispredict = true;
  }

  void remove() {
    assert(seq != 0);
    prev->next = next;
    next->prev = prev;
    if(next_mem) {
      next_mem->prev_mem=0;
    }
    prev_mem = 0;
    next_mem = 0;
    producers.clear();
    mem_pred = 0;
    cache_pred = 0;
  }

  void regfile_access(bool isWrite) {
    if(isWrite) {
      regfile_write++;
    } else{
      regfile_read++;
    }
  }
  void regfile_faccess(bool isWrite) {
    if(isWrite) {
      regfile_fwrite++;
    } else{
      regfile_fread++;
    }
  }
  void rob_access(bool isWrite) {
    if(isWrite) {
      rob_write++;
    } else{
      rob_read++;
    }
  }
  void iw_access(bool isWrite) {
    if(isWrite) {
      iw_write++;
    } else{
      iw_read++;
    }
  }
  void rename_access(bool isWrite) {
    if(isWrite) {
      rename_write++;
    } else{
      rename_read++;
    }
  }

  CP_NodeDiskImage getImage(uint64_t prev_fetch)
  {
    assert(prev_fetch <= fetch_cycle);
    assert(fetch_cycle <= dispatch_cycle);
    assert(dispatch_cycle <= ready_cycle);
    assert(ready_cycle <= execute_cycle);
    assert(execute_cycle <= complete_cycle);
    assert(complete_cycle <= committed_cycle);
    assert(!squashed);
    assert(fetch_cycle - prev_fetch >= icache_lat);

   assert(mem_pred == NULL ||
          (index - mem_pred->index) <= 2048);

    CP_NodeDiskImage img(fetch_cycle - prev_fetch,
                         icache_lat,
                         dispatch_cycle - fetch_cycle,
                         ready_cycle - fetch_cycle,
                         execute_cycle - fetch_cycle,
                         complete_cycle - fetch_cycle,
                         committed_cycle - fetch_cycle,
                         (startwb_cycle) ? (startwb_cycle - fetch_cycle) : 0,
                         (donewb_cycle) ? (donewb_cycle - fetch_cycle) : 0,
                         ctrl_mispredict,
                         spec_mispredict, isload, isstore,
                         (mem_pred)?(this->index - mem_pred->index): 0,
                         (cache_pred)?(this->index - cache_pred->index): 0,
                         ctrl, call, ret,
                         serialBefore, serialAfter,
                         nonSpec, storeCond, prefetch,
                         integer, floating, squashAfter, writeBar,
                         memBar, syscall,
                         pc, micropc, opclass,
                         eff_addr,eff_addr2-eff_addr+1,
                         kernelStart, kernelStop,
                         regfile_read, regfile_write,
                         regfile_fread, regfile_fwrite,
                         rob_read, rob_write,
                         iw_read, iw_write,
                         rename_read, rename_write,
                         seq
                         );
    for (unsigned i = 0, e = producers.size(); i != e; ++i) {
      CP_NodePtr prod = producers[i];
      assert(this->index > prod->index);
      assert(this->execute_cycle >= prod->complete_cycle);
      img.addProd(i, this->index - prod->index);
    }
    return img;
  }
  uint16_t  getOpclass(OpClass opclass);
};



class CP_Graph: public RefCounted {
  BaseCPU *_cpu;

  std::map<uint64_t, CP_NodePtr> seq2Nodes;
  std::map<unsigned, uint64_t> reg_to_producer;

  CP_NodePtr _head;
  CP_NodePtr _tail;
  CP_NodePtr _prev_mem;

  uint64_t _size;
  ogzstream out;

  static std::vector<CP_GraphPtr> *CPGs;

  const char* name() {return "critpath";}

public:
  CP_Graph(BaseCPU *cpu);

  static std::vector<CP_GraphPtr> &getCPGs() {
      if (!CPGs)
          CPGs = new std::vector<CP_GraphPtr>;
      return *CPGs;
  }
  static void deleteCPGs() {
      if (CPGs)
          delete CPGs;
  }
  //Callstack status
  uint64_t prevPC;
  uint16_t prevUPC;
  bool prevCtrl, prevCall, prevRet;
  std::vector<uint64_t> callStackAddrs;
  void trackCallstack(uint64_t addr, uint16_t upc, bool isCtrl, bool isCall,bool isRet);
  void dumpCallstack();

  //trace pipeline status
  void fetch(uint64_t seq);
  void icache_latency(uint64_t seq, uint64_t latency);
  void dispatch(uint64_t seq);
  void ready(uint64_t seq);
  void execute(uint64_t seq);
  void complete(uint64_t seq);
  void committed(uint64_t seq);

  void startWB(uint64_t seq);
  void doneWB(uint64_t seq);

  void squash(uint64_t seq);

  void producer(unsigned reg, uint64_t seq);
  void consumer(unsigned reg, uint64_t seq);
  void setInstTy(uint64_t seq,
     uint64_t pc, uint16_t micropc, OpClass opclass,
     bool ld, bool st, bool ctrl, bool call, bool ret,
     bool kernelStart, bool kernelStop,
     bool serialBefore, bool serialAfter, bool nonSpec, bool storeCond,
     bool prefetch, bool integer, bool floating, bool squashAfter,
     bool writeBar, bool memBar, bool syscall);

  void data_dep(uint64_t src_seq, uint64_t dest_seq);
  void ctrl_dep(uint64_t src_seq);
  void spec_dep(uint64_t src_seq);
  void eff_addr(uint64_t seq, uint64_t addr, uint64_t addr2);

  //Energy Events
  void regfile_access(uint64_t seq, bool isWrite);
  void regfile_faccess(uint64_t seq, bool isWrite);
  void rob_access(uint64_t seq, bool isWrite);
  void iw_access(uint64_t seq, bool isWrite);
  void rename_access(uint64_t seq, bool isWrite);

  void store_to_disk(bool all);

  CP_NodePtr queryNode(uint64_t);
  CP_NodePtr getNode(uint64_t, bool nocreate = true);
  void removeNode(uint64_t);
};

#endif
