
#ifndef __CRITPATH_NODE_HH
#define __CRITPATH_NODE_HH

#include <iostream>


struct CP_NodeDiskImage {
public:
  CP_NodeDiskImage(): _pc(0), _upc(0), _opclass(0),
                      _fc(0), _icache_lat(0),
                      _dc(0),
                      _ec(0),_cc(0),_cmpc(0),
                      _wc(0),_xc(0),
                      _mem_prod(0), _cache_prod(0),
                      _ctrl_miss(false),
                      _spec_miss(false),
                      _isload(false), _isstore(false),
                      _isctrl(false), _iscall(false), _isreturn(false),
                      _serialBefore(false), _serialAfter(false),
                      _nonSpec(false), _storeCond(false), _prefetch(false),
                      _integer(false), _floating(false),
                      _squashAfter(false), _writeBar(false),
                      _memBar(false), _syscall(false),

                      _kernel_start(false), _kernel_stop(false),
                      _numSrcRegs(0), _numFPDestRegs(0), _numIntDestRegs(0),
                      _regfile_read(0), _regfile_write(0),
                      _regfile_fread(0), _regfile_fwrite(0),
                      _rob_read(0), _rob_write(0),
                      _iw_read(0), _iw_write(0),
                      _rename_read(0), _rename_write(0)
  {
  }

  CP_NodeDiskImage(uint16_t fc, uint16_t ic, uint16_t dc,
                   uint16_t rc, uint16_t ec, uint16_t cc, uint16_t cmpc,
                   uint16_t wc, uint16_t xc,
                   bool ctrl_miss, bool spec_miss,
                   bool ld, bool st,
                   uint16_t mp, uint16_t cp,
                   bool ctrl, bool call, bool ret,
                   bool serialBefore, bool serialAfter,
                   bool nonSpec, bool storeCond, bool prefetch,
                   bool integer, bool floating, bool squashAfter,
                   bool writeBar, bool memBar, bool syscall,
                   uint64_t pc, uint16_t upc,
                   uint16_t opclass,
                   uint64_t eff_addr, uint8_t acc_size,
                   bool kernelStart, bool kernelStop,
                   uint8_t numSrcRegs, uint8_t numFPDestRegs, uint8_t numIntDestRegs,
                   uint8_t regfile_read, uint8_t regfile_write,
                   uint8_t regfile_fread, uint8_t regfile_fwrite,
                   uint8_t rob_read, uint8_t rob_write,
                   uint8_t iw_read, uint8_t iw_write,
                   uint8_t rename_read, uint8_t rename_write,
                   uint32_t seq
                   ):
    _pc(pc),
    _upc(upc),
    _opclass(opclass),
    _eff_addr(eff_addr), _acc_size(acc_size),
    _fc(fc),  _icache_lat(ic), _dc(dc), _rc(rc),
    _ec(ec), _cc(cc), _cmpc(cmpc),
    _wc(wc), _xc(xc),
    _mem_prod(mp), _cache_prod(cp),
    _ctrl_miss(ctrl_miss),
    _spec_miss(spec_miss),
    _isload(ld), _isstore(st),
    _isctrl(ctrl), _iscall(call), _isreturn(ret),
    _serialBefore(serialBefore), _serialAfter(serialAfter),
    _nonSpec(nonSpec), _storeCond(storeCond), _prefetch(prefetch),
    _integer(integer), _floating(floating),
    _squashAfter(squashAfter), _writeBar(writeBar),
    _memBar(memBar), _syscall(syscall),
    _kernel_start(kernelStart), _kernel_stop(kernelStop),
    _numSrcRegs(numSrcRegs), _numFPDestRegs(numFPDestRegs),
    _numIntDestRegs(numIntDestRegs),
    _regfile_read(regfile_read), _regfile_write(regfile_write),
    _regfile_fread(regfile_fread), _regfile_fwrite(regfile_fwrite),
    _rob_read(rob_read), _rob_write(rob_write),
    _iw_read(iw_read), _iw_write(iw_write),
    _rename_read(rename_read), _rename_write(rename_write),
    _seq(seq)
  {
    verify();
    for (int i = 0; i < 7; ++i) {
      _prod[i] = 0;
    }
  }
  void addProd(int i, uint16_t p) {
    _prod[i] = p;
  }

  uint64_t _pc;
  uint16_t _upc;
  uint16_t _opclass;

  uint64_t _eff_addr;
  uint8_t  _acc_size;

  uint16_t _fc;  //fetch to fetch cycle
  uint16_t _icache_lat;
  uint16_t _dc;  //fetch to decode cycle
  uint16_t _rc;  //fetch to ready cycle
  uint16_t _ec;  //fetch to execute cycle
  uint16_t _cc;  //fetch to commit cycle
  uint16_t _cmpc; //fetch to complete cycle
  //STORES ONLY:
  uint16_t _wc;  //fetch to writeback cycle
  uint16_t _xc;  //fetch to xompletly done cycle

  uint16_t _prod[7]; //MaxInstSrcRegs for X86
  uint16_t _mem_prod; // memory prod
  uint16_t _cache_prod; // cache prod
  bool     _ctrl_miss:1;    //ctrl mispredict
  bool     _spec_miss:1;    //spec mistpredict
  bool     _isload:1;  // is a load inst
  bool     _isstore:1; // is a store inst
  bool     _isctrl:1;
  bool     _iscall:1;
  bool     _isreturn:1;
  bool _serialBefore:1, _serialAfter:1, _nonSpec:1;
  bool _storeCond:1, _prefetch:1, _integer:1, _floating:1;
  bool _squashAfter:1, _writeBar:1, _memBar:1, _syscall:1;
  bool _kernel_start:1;
  bool _kernel_stop:1;

  uint8_t _numSrcRegs, _numFPDestRegs, _numIntDestRegs;
  uint8_t _regfile_read, _regfile_write;
  uint8_t _regfile_fread, _regfile_fwrite;
  uint8_t _rob_read, _rob_write;
  uint8_t _iw_read, _iw_write;
  uint8_t _rename_read, _rename_write;

  uint32_t _seq;

  #ifdef STANDALONE_CRITPATH
  void write_to_stream(std::ostream& out) {
    out << _fc << "[" << _icache_lat  << "],"
        <<_dc << "," << _ec << "," << _cc << "," << _cmpc
        << " [ " << _prod[0] << " "
                 << _prod[1] << " "
                 << _prod[2] << " "
                 << _prod[3] << " "
                 << _prod[4] << " "
                 << _prod[5] << " "
                 << _prod[6] << ",m " << _mem_prod << ",c " << _cache_prod << "] "
        << ((_ctrl_miss||_spec_miss)?"M":" ") //<< "," << _spec_miss << ","
        << ((_isload)?"L":"") << ((_isstore)?"S":"")
        << ((_isctrl)?"C":"") << ((_iscall)?"F":"") << ((_isreturn)?"R":"")
        << ", [" << _pc << ":" << _upc << "],"
        << ", [" << _eff_addr << ":" << _acc_size << "],"
        << _opclass << "{" << _kernel_start << "," << _kernel_stop << "} "
        << " reg{" << _regfile_read << "," << _regfile_write << "}"
        << " freg{" << _regfile_fread << "," << _regfile_fwrite << "}"
        << " rob{" << _rob_read << "," << _rob_write << "}"
        << " iw{" <<  _iw_read << "," << _iw_write << "}"
        << " ren{" << _rename_read << "," << _rename_write << "}\n";
      //<< "\n";
  }

  static CP_NodeDiskImage read_from_file(std::istream &in) {
    CP_NodeDiskImage img;
    in.read((char*)&img, sizeof(img));
    img.verify();
    return img;
  }

  static void read_from_file_into(std::istream &in,CP_NodeDiskImage& img) {
    in.read((char*)&img, sizeof(img));
    img.verify();
  }

  #else
  void write_to_stream(std::ostream &out);
  #endif
  void verify() {
    assert(_dc <= _ec);
    assert(_ec <= _cc);
    assert(_cc <= _cmpc);
  }


};

#endif
