# Copyright (c) 2006-2007 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Steve Reinhardt

import m5
from m5.objects import *
m5.util.addToPath('../configs/common')
from Caches import *

cpu = DerivO3CPU(cpu_id=0)
cpu.addTwoLevelCacheHierarchy(L1Cache(size = '128kB'),
                              L1Cache(size = '256kB'),
                              L2Cache(size = '2MB'))
# @todo Note that the L2 latency here is unmodified and 2 cycles,
# should set hit latency and response latency to 20 cycles as for
# other scripts
cpu.clock = '2GHz'

system = System(cpu = cpu,
                physmem = DDR3_1600_x64(),
                membus = CoherentBus(),
                mem_mode = "timing")
system.clock = '1GHz'
system.system_port = system.membus.slave
system.physmem.port = system.membus.master
# create the interrupt controller
cpu.createInterruptController()
cpu.connectAllPorts(system.membus)

root = Root(full_system = False, system = system)
