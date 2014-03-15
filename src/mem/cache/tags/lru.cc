/*
 * Copyright (c) 2012-2013 ARM Limited
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
 * Authors: Erik Hallnor
 */

/**
 * @file
 * Definitions of LRU tag store.
 */

#include <string>

#include "base/intmath.hh"
#include "debug/Cache.hh"
#include "debug/CacheRepl.hh"
#include "mem/cache/tags/lru.hh"
#include "mem/cache/base.hh"
#include "sim/core.hh"



using namespace std;

LRU::LRU(const Params *p)
    :BaseTags(p), assoc(p->assoc),
     numSets(p->size / (p->block_size * p->assoc))
{
    // Check parameters
    if (blkSize < 4 || !isPowerOf2(blkSize)) {
        fatal("Block size must be at least 4 and a power of 2");
    }
    if (numSets <= 0 || !isPowerOf2(numSets)) {
        fatal("# of sets must be non-zero and a power of 2");
    }
    if (assoc <= 0) {
        fatal("associativity must be greater than zero");
    }
    if (hitLatency <= 0) {
        fatal("access latency must be greater than zero");
    }

    blkMask = blkSize - 1;
    setShift = floorLog2(blkSize);
    setMask = numSets - 1;
    tagShift = setShift + floorLog2(numSets);
    warmedUp = false;
    /** @todo Make warmup percentage a parameter. */
    warmupBound = numSets * assoc;

    sets = new SetType[numSets];
    blks = new BlkType[numSets * assoc];
    // allocate data storage in one big chunk
    numBlocks = numSets * assoc;
    dataBlks = new uint8_t[numBlocks * blkSize]();

    #ifdef BIT_FREQ_HIST
    firstCycleAccessed=0;
    lastCycleAccessed=0;

    cycleOfBit = new uint64_t[numBlocks * blkSize * 8];
    cyclesOf1 = new uint64_t[numBlocks * blkSize * 8];
    downTrans = new uint32_t[numBlocks * blkSize * 8];
    dataBlks_old = new uint8_t[numBlocks * blkSize]();
    getLRUs().push_back(this);
    #endif


    unsigned blkIndex = 0;       // index into blks array
    for (unsigned i = 0; i < numSets; ++i) {
        sets[i].assoc = assoc;

        sets[i].blks = new BlkType*[assoc];

        // link in the data blocks
        for (unsigned j = 0; j < assoc; ++j) {
            // locate next cache block
            BlkType *blk = &blks[blkIndex];
            blk->data = &dataBlks[blkSize*blkIndex];

            #ifdef BIT_FREQ_HIST
            blk->data_old = &dataBlks_old[blkSize*blkIndex];
            blk->cycleOfBit = &cycleOfBit[blkSize*blkIndex*8];
            blk->cyclesOf1 = &cyclesOf1[blkSize*blkIndex*8];
            blk->downTrans = &downTrans[blkSize*blkIndex*8];
            #endif


            ++blkIndex;

            // invalidate new cache block
            blk->invalidate();

            //EGH Fix Me : do we need to initialize blk?

            // Setting the tag to j is just to prevent long chains in the hash
            // table; won't matter because the block is invalid
            blk->tag = j;
            blk->whenReady = 0;
            blk->isTouched = false;
            blk->size = blkSize;
            sets[i].blks[j]=blk;
            blk->set = i;
        }
    }
}


void LRU::mayHaveUpdated(BlkType* blk, const char* msg, bool force_update)
{
#ifdef BIT_FREQ_HIST

  if(blk==NULL) {
    return;
  }

  if(firstCycleAccessed==0) {
    firstCycleAccessed=curCycle();

    //Get Last Cycles of 1
    unsigned blkIndex = 0;       // index into blks array
    for (unsigned i = 0; i < numSets; ++i) {
      for (unsigned j = 0; j < assoc; ++j) {
        BlkType *blk = &blks[blkIndex];
        //mayHaveUpdated(blk,true);
    
        for(int byte = 0; byte < blk->size; ++byte ) {
          for(int bit = 0; bit < 8; ++bit ) {
            int bit_index = 8 * byte + bit;
            blk->cycleOfBit[bit_index] = curCycle();
            blk->downTrans[bit_index] = 0;
            blk->cyclesOf1[bit_index] = 0;
          }
          blk->data_old[byte]=blk->data[byte];   
        }
        ++blkIndex;
      }
    }
  }
  lastCycleAccessed=curCycle();
  //DPRINTF(Cache, "may have accessed set %d using %s\n", blk->set, msg);

  //iterate through bits in the cache block, see if they'e been updated
  //if so:  1. record in the appropriate histogram, the length 
  //        2. udpate the cycle type of that bit
  //Copy the data onto the olddata

  //bool updated=false;

  for(int byte = 0; byte < blk->size; ++byte ) {
    uint8_t new_byte=blk->data[byte];
    uint8_t old_byte=blk->data_old[byte];
    if(new_byte==old_byte && !force_update) {
      continue;
    }

    for(int bit = 0; bit < 8; ++bit ) {
      uint8_t new_bit = getbit(new_byte,bit);
      uint8_t old_bit = getbit(old_byte,bit);

      if(new_bit!=old_bit || force_update) {
        //DPRINTF(Cache, "BYTE %d, BIT %d  Got Updated!\n",byte,bit);
        int bit_index = 8 * byte + bit;
        uint64_t cycleDiff = curCycle() - blk->cycleOfBit[bit_index];
        if(!force_update && new_bit == 0) {
          blk->downTrans[bit_index] += 1;
          blk->cyclesOf1[bit_index] += cycleDiff;
        }
        if(force_update && new_bit == 1) {
          blk->cyclesOf1[bit_index] += cycleDiff;
        }
        assert(blk->cycleOfBit[bit_index] <= curCycle());
        blk->cycleOfBit[bit_index] = curCycle();
        //onesHisto[cycleDiff] +=1;
      }
    }
    blk->data_old[byte]=new_byte;
  }
#endif

} 
#ifdef BIT_FREQ_HIST
std::vector<LRUPtr> *LRU::LRUs = 0;
uint8_t LRU::getbit(uint8_t data, int index) {
  return ((data >> index) & 0x01);
}

void LRU::printInfo() {
  std::map<uint64_t, uint32_t>::iterator i,e;
  ofstream raw_file, pic_file;
  raw_file.open((std::string(name())+std::string(".wear.csv").c_str()),
                                  std::ofstream::out | std::ofstream::trunc);

  pic_file.open((std::string(name())+std::string(".wear.ppm").c_str()),
                                  std::ofstream::out | std::ofstream::trunc);

  //Generate Grayscale PPM File Header
  pic_file << "P2\n";
  pic_file << blkSize*assoc*8 << " " << numSets << " \n";
  pic_file <<  65535 << " \n";

  uint64_t total_cycles = curCycle() - firstCycleAccessed;

  //put some comments in the file
  pic_file << "#assoc: " << assoc << "\n";
  pic_file << "#numSets: " << numSets << "\n";
  pic_file << "#blkSize: " << blkSize << " (bytes)\n";
  pic_file << "#numBits: " << 8*numSets*assoc*blkSize << "\n\n";
  pic_file << "#firstCycle: " << firstCycleAccessed << "\n";
  pic_file << "#lastCycle: " << lastCycleAccessed << "\n";
  pic_file << "#curCycle: " << curCycle() << "\n";
  pic_file << "#total_cycles: " << total_cycles << "\n";

  if(total_cycles==0) {
    total_cycles=1;
  }

  //Get Last Cycles of 1
  unsigned blkIndex = 0;       // index into blks array
  for (unsigned i = 0; i < numSets; ++i) {
    for (unsigned j = 0; j < assoc; ++j) {
      BlkType *blk = &blks[blkIndex];
      mayHaveUpdated(blk,"end",true);
  
      for(int byte = 0; byte < blk->size; ++byte ) {
        for(int bit = 0; bit < 8; ++bit ) {
          int bit_index = 8 * byte + bit;
//          DPRINTF(Cache, "%d,%d,%d: %lld %d\n",blkIndex,byte,bit,
//                                               cyclesOf1[bit_index],
//                                               downTrans[bit_index]);

          raw_file << blk->cyclesOf1[bit_index] << "," << blk->downTrans[bit_index] << " ";
          pic_file << (65535*(blk->cyclesOf1[bit_index] - blk->downTrans[bit_index]/1000))/total_cycles << " ";
        }
      }
      raw_file << "\n";
      pic_file << "\n"; 
      ++blkIndex;
    }
  }

  raw_file.close();
  pic_file.close();
  //for(i = onesHisto.begin(), e = onesHisto.end(); i!=e; ++i) {
  //  DPRINTF(Cache, "%d:%d\n",i->first, i->second);
  //}
  //DPRINTF(Cache, "Printing Info!\n");
}

static void print_lru_info()
{
  for (unsigned i = 0; i < LRU::getLRUs().size(); ++i) {
    LRUPtr lru = LRU::getLRUs()[i];
    lru->printInfo();
    //if (g->getNumNodesWrote() == 0)
    //  g->delete_file();
  }
  //std::cerr << "Num of Nodes newed......: " << CP_Node::_total_count << "\n";
  //std::cerr << "Num of Nodes deleted....: " << CP_Node::_del_count << "\n";
  //std::cerr << "Num of Nodes not deleted: " << CP_Node::_count << "\n";
  //CP_Graph::deleteCPGs();
}

__attribute__((constructor))
static void init()
{
  atexit(print_lru_info);
}


#endif


LRU::~LRU()
{
    delete [] dataBlks;
    delete [] blks;
    delete [] sets;
}

LRU::BlkType*
LRU::accessBlock(Addr addr, Cycles &lat, int master_id)
{
    Addr tag = extractTag(addr);
    unsigned set = extractSet(addr);
    BlkType *blk = sets[set].findBlk(tag);
    lat = hitLatency;
    if (blk != NULL) {
        // move this block to head of the MRU list
        sets[set].moveToHead(blk);
        DPRINTF(CacheRepl, "set %x: moving blk %x to MRU\n",
                set, regenerateBlkAddr(tag, set));
        if (blk->whenReady > curTick()
            && cache->ticksToCycles(blk->whenReady - curTick()) > hitLatency) {
            lat = cache->ticksToCycles(blk->whenReady - curTick());
        }
        blk->refCount += 1;
    }

    return blk;
}


LRU::BlkType*
LRU::findBlock(Addr addr) const
{
    Addr tag = extractTag(addr);
    unsigned set = extractSet(addr);
    BlkType *blk = sets[set].findBlk(tag);
    return blk;
}

LRU::BlkType*
LRU::findVictim(Addr addr, PacketList &writebacks)
{
    unsigned set = extractSet(addr);
    // grab a replacement candidate
    BlkType *blk = sets[set].blks[assoc-1];

    if (blk->isValid()) {
        DPRINTF(CacheRepl, "set %x: selecting blk %x for replacement\n",
                set, regenerateBlkAddr(blk->tag, set));
    }
    return blk;
}

void
LRU::insertBlock(PacketPtr pkt, BlkType *blk)
{
    Addr addr = pkt->getAddr();
    MasterID master_id = pkt->req->masterId();
    if (!blk->isTouched) {
        tagsInUse++;
        blk->isTouched = true;
        if (!warmedUp && tagsInUse.value() >= warmupBound) {
            warmedUp = true;
            warmupCycle = curTick();
        }
    }

    // If we're replacing a block that was previously valid update
    // stats for it. This can't be done in findBlock() because a
    // found block might not actually be replaced there if the
    // coherence protocol says it can't be.
    if (blk->isValid()) {
        replacements[0]++;
        totalRefs += blk->refCount;
        ++sampledRefs;
        blk->refCount = 0;

        // deal with evicted block
        assert(blk->srcMasterId < cache->system->maxMasters());
        occupancies[blk->srcMasterId]--;

        blk->invalidate();
    }

    blk->isTouched = true;
    // Set tag for new block.  Caller is responsible for setting status.
    blk->tag = extractTag(addr);

    // deal with what we are bringing in
    assert(master_id < cache->system->maxMasters());
    occupancies[master_id]++;
    blk->srcMasterId = master_id;

    unsigned set = extractSet(addr);
    sets[set].moveToHead(blk);
}

void
LRU::invalidate(BlkType *blk)
{
    assert(blk);
    assert(blk->isValid());
    tagsInUse--;
    assert(blk->srcMasterId < cache->system->maxMasters());
    occupancies[blk->srcMasterId]--;
    blk->srcMasterId = Request::invldMasterId;

    // should be evicted before valid blocks
    unsigned set = blk->set;
    sets[set].moveToTail(blk);
}

void
LRU::clearLocks()
{
    for (int i = 0; i < numBlocks; i++){
        blks[i].clearLoadLocks();
    }
}

LRU *
LRUParams::create()
{
    return new LRU(this); 
}
std::string
LRU::print() const {
    std::string cache_state;
    for (unsigned i = 0; i < numSets; ++i) {
        // link in the data blocks
        for (unsigned j = 0; j < assoc; ++j) {
            BlkType *blk = sets[i].blks[j];
            if (blk->isValid())
                cache_state += csprintf("\tset: %d block: %d %s\n", i, j,
                        blk->print());
        }
    }
    if (cache_state.empty())
        cache_state = "no valid tags\n";
    return cache_state;
}

void
LRU::cleanupRefs()
{
    for (unsigned i = 0; i < numSets*assoc; ++i) {
        if (blks[i].isValid()) {
            totalRefs += blks[i].refCount;
            ++sampledRefs;
        }
    }
}
