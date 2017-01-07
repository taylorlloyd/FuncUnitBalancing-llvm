# Functional Unit Balancing for GPUs on LLVM

This repository contains prototype code for an LLVM transformation that attempts to prevent overuse of any single set of functional units on a GPU.
Because of the way GPUs implement instruction scheduling, better balancing of functional units can prevent pipeline stalls, improving overall throughput.
