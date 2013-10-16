
#include <cstdlib>
#include <fstream>
#include <utility>

#include "cpu/base.hh"
#include "debug/CP.hh"
#include "crtpath.hh"

#define DISABLE_CP (getenv("GEM5_CRITICAL_PATH") == NULL)
#define CP_TEXT 0

std::vector<CP_GraphPtr> *CP_Graph::CPGs = 0;
uint64_t CP_Node::_count = 0;
uint64_t CP_Node::_total_count = 0;
uint64_t CP_Node::_del_count = 0;



void CP_Graph::trackCallstack(uint64_t addr, uint16_t upc, bool isCtrl,
                              bool isCall,bool isRet)
{
  if (DISABLE_CP)
    return;

  if (prevCall)
    callStackAddrs.push_back(addr);
  else if (prevRet)
    callStackAddrs.pop_back();

  prevPC = addr;
  prevUPC = upc;
  prevCtrl = isCtrl;
  prevCall = isCall;
  prevRet = isRet;
}

void CP_Graph::dumpCallstack()
{
  if (DISABLE_CP)
    return;

  std::ofstream stackout;
  stackout.open("callstack.out", std::ios::out);
  assert(stackout.is_open());
  stackout << prevPC << "\n";
  stackout << prevUPC << "\n";
  stackout << prevCtrl << "\n";
  stackout << prevCall << "\n";
  stackout << prevRet << "\n";
  for (int i = 0; i < callStackAddrs.size();++i) {
    stackout << callStackAddrs[i] << "\n";
  }
}

void CP_Graph::regfile_access(uint64_t seq, bool isWrite)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "Regfile_%s: %lli [cycle: %lli]\n", isWrite?("Write"):("Read"), seq, _cpu->curCycle());
  getNode(seq, false)->regfile_access(isWrite);
}

void CP_Graph::regfile_faccess(uint64_t seq, bool isWrite)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "Regfile_f_%s: %lli [cycle: %lli]\n", isWrite?("Write"):("Read"), seq, _cpu->curCycle());
  getNode(seq, false)->regfile_faccess(isWrite);
}

void CP_Graph::rob_access(uint64_t seq, bool isWrite)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "ROB_%s: %lli [cycle: %lli]\n", isWrite?("Write"):("Read"), seq, _cpu->curCycle());
  getNode(seq, false)->rob_access(isWrite);
}

void CP_Graph::iw_access(uint64_t seq, bool isWrite)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "IW_%s: %lli [cycle: %lli]\n", isWrite?("Write"):("Read"), seq, _cpu->curCycle());
  getNode(seq, false)->iw_access(isWrite);
}

void CP_Graph::rename_access(uint64_t seq, bool isWrite)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "Rename_%s: %lli [cycle: %lli]\n", isWrite?("Write"):("Read"), seq, _cpu->curCycle());
  getNode(seq, false)->rename_access(isWrite);
}



void CP_Graph::fetch(uint64_t seq)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "Fetch: %lli [cycle: %lli]\n", seq, _cpu->curCycle());
  getNode(seq, false)->fetch(_cpu->curCycle());
}

void CP_Graph::icache_latency(uint64_t seq, uint64_t latency)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "Fetch: %lli icache latency = %lli \n", seq, latency);
  getNode(seq)->icache_latency(latency);
}

void CP_Graph::dispatch(uint64_t seq)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "Dispatch: %lli [cycle: %lli]\n", seq, _cpu->curCycle());
  getNode(seq)->dispatch(_cpu->curCycle());
}

void CP_Graph::ready(uint64_t seq)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "Ready: %lli [cycle: %lli]\n", seq, _cpu->curCycle());
  getNode(seq)->ready(_cpu->curCycle());
}

void CP_Graph::execute(uint64_t seq)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "Execute: %lli [cycle: %lli]\n", seq, _cpu->curCycle());
  CP_NodePtr ptr = getNode(seq);
  ptr->execute(_cpu->curCycle());
}

void CP_Graph::complete(uint64_t seq)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "Complete: %lli [cycle: %lli]\n", seq, _cpu->curCycle());
  getNode(seq)->complete(_cpu->curCycle());
}
void CP_Graph::committed(uint64_t seq)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "Committed: %lli [cycle: %lli]\n", seq, _cpu->curCycle());
  CP_NodePtr ptr = getNode(seq);
  getNode(seq)->committed(_cpu->curCycle());

  if (ptr->squashed)
    removeNode(seq);
  else {
    for (std::map<uint64_t, bool>::iterator I = ptr->prod_seq.begin(),
           E = ptr->prod_seq.end(); I != E; ++I) {
      CP_NodePtr prod = queryNode(I->first);
      if (!prod)
        continue;
      assert(ptr->execute_cycle >= prod->complete_cycle);
      ptr->data_dep(prod);
    }
  }
}

uint64_t lastWB=0;

void CP_Graph::retryWB() {
  assert(lastWB!=0);

  DPRINTF(CP, "Retry -- StartWB: %lli [cycle: %lli]\n", lastWB, _cpu->curCycle());
  getNode(lastWB)->startWB(_cpu->curCycle());
  //lastWB=0; this might make it crash...
}

void CP_Graph::startWB(uint64_t seq)
{
  if (DISABLE_CP)
    return;

  //Writeback might get updated with a retry if the cache is blocked.
  //lets remember the "cycle" that
  lastWB=seq;

  DPRINTF(CP, "StartWB: %lli [cycle: %lli]\n", seq, _cpu->curCycle());
  getNode(seq)->startWB(_cpu->curCycle());
}

void CP_Graph::doneWB(uint64_t seq)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "DoneWB: %lli [cycle: %lli]\n", seq, _cpu->curCycle());
  if(getNode(seq)) {
    getNode(seq)->doneWB(_cpu->curCycle());
  }
}


void CP_Graph::squash(uint64_t seq)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "Squash: %lli [cycle: %lli]\n", seq, _cpu->curCycle());
  getNode(seq)->squashed = true;
}


void CP_Graph::producer(unsigned reg, uint64_t seq)
{
  if (DISABLE_CP)
    return;

  reg_to_producer[reg] = seq;
}

void CP_Graph::consumer(unsigned reg, uint64_t seq)
{
  if (DISABLE_CP)
    return;

  uint64_t prod_seq = reg_to_producer[reg];
  if (prod_seq) {
    assert(prod_seq < seq);
    data_dep(prod_seq, seq);
  }
}

void CP_Graph::setInstTy(uint64_t seq,
     uint64_t pc, uint16_t micropc, OpClass opclass,
     uint8_t numSrcRegs, uint8_t numFPDestRegs, uint8_t numIntDestRegs,
     bool ld, bool st, bool ctrl, bool call, bool ret,
     bool kernelStart, bool kernelStop,
     bool serialBefore, bool serialAfter, bool nonSpec, bool storeCond,
     bool prefetch, bool integer, bool floating, bool squashAfter,
     bool writeBar, bool memBar, bool syscall)
{
  if (DISABLE_CP)
    return;

  CP_NodePtr node = getNode(seq);
  node->numSrcRegs = numSrcRegs;
  node->numFPDestRegs = numFPDestRegs;
  node->numIntDestRegs = numIntDestRegs;
  node->isload = ld;
  node->isstore = st;
  node->ctrl = ctrl;
  node->call = call;
  node->ret = ret;
  node->pc = pc;
  node->micropc = micropc;
  node->opclass = node->getOpclass(opclass);
  node->kernelStart = kernelStart;
  node->kernelStop = kernelStop;
  node->serialBefore = serialBefore;
  node->serialAfter = serialAfter;
  node->nonSpec = nonSpec;
  node->storeCond = storeCond;
  node->prefetch = prefetch;
  node->integer = integer;
  node->floating = floating;
  node->squashAfter = squashAfter;
  node->writeBar = writeBar;
  node->memBar = memBar;
  node->syscall = syscall;

  if (ld || st) {
    assert(node->eff_addr != 0);
    assert(node->eff_addr2 != 0);
    if (_prev_mem == 0) {
      _prev_mem = node;
    } else {
      node->prev_mem = _prev_mem;
      _prev_mem->next_mem = node;
      _prev_mem = node;

      CP_NodePtr cur_mem_node = node->prev_mem;

      // 16 bytes <- O3CPU default params
      uint64_t node_eff_addr = node->eff_addr >> 0; //should really read this in...
      uint64_t node_eff_addr2 = node->eff_addr2 >> 0;

      while (cur_mem_node) {
        uint64_t cur_eff_addr = cur_mem_node->eff_addr >> 0;
        uint64_t cur_eff_addr2 = cur_mem_node->eff_addr2 >> 0;

        if (node_eff_addr2 >= cur_eff_addr
            && node_eff_addr <= cur_eff_addr2) {
          node->mem_pred = cur_mem_node;
          break;
        }
        cur_mem_node = cur_mem_node->prev_mem;
      }

      cur_mem_node = node->prev_mem;
      // 64 bytes <- O3CPU default params for caches (TODO: set from config)
      node_eff_addr = node->eff_addr >> 6;
      node_eff_addr2 = node->eff_addr2 >> 6;

      while (cur_mem_node) {
        uint64_t cur_eff_addr = cur_mem_node->eff_addr >> 6;
        uint64_t cur_eff_addr2 = cur_mem_node->eff_addr2 >> 6;

        if (node_eff_addr2 >= cur_eff_addr
            && node_eff_addr <= cur_eff_addr2) {
          node->cache_pred = cur_mem_node;
          //figure out this mem access brought in the memory for me
          //right now, we are saying this is true if they overlap.
          if ( node->memCompletionTime() > node->cache_pred->memRequestTime()
            && node->memRequestTime() < node->cache_pred->memCompletionTime()) {
            node->true_cache_prod=true;
          }

          break;
        }
        cur_mem_node = cur_mem_node->prev_mem;
      }
    }
  }
}

void CP_Graph::data_dep(uint64_t src_seq, uint64_t dest_seq)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "DataDep: %lli -> %lli [cycle: %lli]\n",
          src_seq, dest_seq, _cpu->curCycle());
  assert(src_seq < dest_seq);
  getNode(dest_seq, true)->prod_seq[src_seq] = true;
}

void CP_Graph::ctrl_dep(uint64_t src_seq)
{
  if (DISABLE_CP)
    return;

  DPRINTF(CP, "CtrlDep %lli -> next [cycle: %lli]\n",
          src_seq, _cpu->curCycle());
  getNode(src_seq)->ctrl_dep();
}

void CP_Graph::spec_dep(uint64_t src_seq)
{

  if (DISABLE_CP)
    return;

  DPRINTF(CP, "SpecDep %lli -> next [cycle: %lli]\n",
          src_seq, _cpu->curCycle());
  getNode(src_seq)->spec_dep();

}

void CP_Graph::eff_addr(uint64_t seq, uint64_t addr, uint64_t addr2)
{
  if (DISABLE_CP)
    return;
  CP_NodePtr node = queryNode(seq);
  if (!node)
    return;

  node->eff_addr = addr;
  node->eff_addr2 = addr2;
}

CP_NodePtr CP_Graph::queryNode(uint64_t seq)
{

  if (DISABLE_CP)
    seq = 1;

  std::map<uint64_t, CP_NodePtr>::iterator seqIT = seq2Nodes.find(seq);
  if (seqIT != seq2Nodes.end())
    return seqIT->second;

  return 0;
}
CP_NodePtr CP_Graph::getNode(uint64_t seq, bool nocreate)
{

  if (DISABLE_CP)
    seq = 1;

  std::map<uint64_t, CP_NodePtr>::iterator seqIT = seq2Nodes.find(seq);
  if (seqIT != seq2Nodes.end())
    return seqIT->second;
  if (nocreate)
    return 0;

  CP_NodePtr ptr = new CP_Node(seq);

  ptr->prev = _tail->prev ;
  ptr->prev->next = ptr;

  ptr->next = _tail;
  _tail->prev = ptr;

  ++_size;
  seq2Nodes.insert(std::make_pair(seq, ptr));

  assert (ptr->prev->seq < ptr->seq);

  if (_size > 2048) {
    //store the first 1024 nodes to disk
    store_to_disk(false);
  }
  return ptr;
}

void CP_Graph::removeNode(uint64_t seq)
{
  if (DISABLE_CP)
    return;


  std::map<uint64_t, CP_NodePtr>::iterator seqIT = seq2Nodes.find(seq);

  if(_prev_mem == seqIT->second) {
    _prev_mem=NULL;
  }

  if (seqIT == seq2Nodes.end())
    return ;
  seqIT->second->remove();
  seq2Nodes.erase(seqIT);
  -- _size;
}

CP_Node::CP_Node(uint64_t s):
  next(0), prev(0), prev_mem(0), next_mem(0),
  seq(s), index(0),
  fetch_cycle(0), icache_lat(0),
  dispatch_cycle(0), ready_cycle(0),
  execute_cycle(0), complete_cycle(0),
  committed_cycle(0), startwb_cycle(0), donewb_cycle(0),
  ctrl_mispredict(false), spec_mispredict(false),
  squashed(false), isload(false), isstore(false),
  true_cache_prod(false),
  kernelStart(false), kernelStop(false),
  eff_addr(0), eff_addr2(0),
  regfile_read(0), regfile_write(0),
  regfile_fread(0), regfile_fwrite(0),
  rob_read(0), rob_write(0),
  iw_read(0), iw_write(0),
  rename_read(0), rename_write(0),
  wrote_to_disk(false),
  mem_pred(0), cache_pred(0)
{
  CP_Node::_count ++;
  CP_Node::_total_count ++;
}

uint16_t CP_Node::getOpclass(OpClass opclass)
{
  return opclass;
}

void CP_Graph::store_to_disk(bool all)
{
  if (DISABLE_CP)
    return;

  unsigned numNodes = 1024;
  if (all) {
    numNodes = _size;
  } else {
    assert(_size > 2048);
  }
  assert(numNodes <= _size);
  unsigned bef_size = _size;

  CP_NodePtr node = _head;
  uint64_t index = node->index;
  uint64_t prev_fetch = node->fetch_cycle;
  bool break_after_this = false;

  for (unsigned i = 0; (i < numNodes) && !break_after_this; ++i) {
    node = node->next;

    assert(node != _tail);
    if (node->squashed) {
      removeNode(node->seq);
      continue;
    }

    if (all) {
       if(node->committed_cycle == 0) {
         break_after_this = true;
         node->committed_cycle = _cpu->curCycle();
       }
       if(node->complete_cycle == 0) {
         break_after_this = true;
         node->complete_cycle = _cpu->curCycle();
       }
       if(node->execute_cycle == 0) {
         break_after_this = true;
         node->execute_cycle = _cpu->curCycle();
       }
       if(node->ready_cycle == 0) {
         break_after_this = true;
         node->ready_cycle = _cpu->curCycle();
       }
       if(node->dispatch_cycle == 0) {
         break_after_this = true;
         node->dispatch_cycle = _cpu->curCycle();
       }
       if(node->fetch_cycle == 0) {
         break_after_this = true;
         node->fetch_cycle = _cpu->curCycle();
       }
       if (node->isstore && node->startwb_cycle == 0) {
         break_after_this = true;
         node->startwb_cycle = _cpu->curCycle();
       }
       if (node->isstore && node->donewb_cycle == 0) {
         break_after_this = true;
         node->donewb_cycle = _cpu->curCycle();
       }
    }
/*
    if (all && node->committed_cycle == 0) {
       node->committed_cycle = _cpu->curCycle();
      break_after_this = true;
    }
*/
    if (!node->wrote_to_disk) {
      //write the node to disk

      assert(prev_fetch <= node->fetch_cycle);
      node->index = index+1;
      DPRINTF(CP, "Getting img for %lli\n", node->seq);
      DPRINTF(CP, "SEQ:%lli -> CPIND:%lli\n", node->seq, node->index-1); //because ind starts at 1...
      CP_NodeDiskImage img = node->getImage(prev_fetch);
      img.write_to_stream(out);
      node->wrote_to_disk = true;
      ++_num_nodes_to_disk;
    }
    index = node->index;
    prev_fetch = node->fetch_cycle;
    removeNode(node->seq);
    assert(bef_size == i + 1 + _size);
    // prevent compiler warning about unused variable for bef_size
    (void)bef_size;
  }
  _head->index = index;
  _head->fetch_cycle = prev_fetch;

  if (all) {
    out.close();
  } else {
    out.flush();
  }
}


void CP_Graph::delete_file()
{
  assert(!out.is_open());
  assert(getNumNodesWrote() == 0);
  assert(_out_file_name != "");
  unlink(_out_file_name.c_str());
}

CP_Graph::CP_Graph(BaseCPU *cpu): _cpu(cpu), _head(0), _tail(0), _size(0),
                                  _num_nodes_to_disk(0), _out_file_name("") {
    static bool first_one = true;

    _head = new CP_Node(0);
    _tail = new CP_Node(0);
    _head->next = _tail;
    _tail->prev = _head;
    getCPGs().push_back(this);

    if (DISABLE_CP)
      return;

    char tmp[32];

    const char *fname = "cp_switch_0.data.gz";
    const char *env_value = getenv("GEM5_CRITICAL_PATH");
    if (first_one) {
      if (strlen(env_value) != 1) {
        // use this value
        fname = env_value;
      } else {
        sprintf(tmp, "cp_switch_%p.data.gz", cpu);
        fname = tmp;
      }
    } else {
      sprintf(tmp, "cp_base_%p.data.gz",cpu);
      fname = tmp;
    }

    if (CP_TEXT) {
      out.open(fname, std::ios::out);
    }  else {
      out.open(fname, std::ios::out | std::ios::binary);
    }
    _out_file_name = std::string(fname);

    assert(out.is_open());
    first_one=false;
  }

#ifndef STANDALONE_CRITPATH
void CP_NodeDiskImage::write_to_stream(std::ostream& out) {
  if (CP_TEXT) {
    out << _dc << "," << _ec << "," << _cc << "," << _cmpc ;
    out << " [ " << _prod[0] << " "
        << _prod[1] << " "
        << _prod[2] << " "
        << _prod[3] << " "
        << _prod[4] << " "
        << _prod[5] << " "
        << _prod[6] << " ] " << _ctrl_miss << "," << _spec_miss <<"\n";
  } else {
    out.write((const char*)this, sizeof(CP_NodeDiskImage));
  }
}
#endif

static void store_all()
{
  if (DISABLE_CP)
    return;

  for (unsigned i = 0; i < CP_Graph::getCPGs().size(); ++i) {
    CP_GraphPtr g = CP_Graph::getCPGs()[i];
    g->store_to_disk(true);
    if (g->getNumNodesWrote() == 0)
      g->delete_file();
  }
  std::cerr << "Num of Nodes newed......: " << CP_Node::_total_count << "\n";
  std::cerr << "Num of Nodes deleted....: " << CP_Node::_del_count << "\n";
  std::cerr << "Num of Nodes not deleted: " << CP_Node::_count << "\n";
  CP_Graph::deleteCPGs();
}

__attribute__((constructor))
static void init()
{
  atexit(store_all);
}
